#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/core/irq.h"
#include "hw/gpio/g233_gpio.h"
#include "migration/vmstate.h"

static void g233_gpio_update_irq(G233GPIOState *s)
{
    uint32_t is = s->regs[G233_GPIO_IS];
    uint32_t ie = s->regs[G233_GPIO_IE];

    qemu_set_irq(s->irq, (is & ie) ? 1 : 0);
}

static void update_state(G233GPIOState *s)
{
    uint32_t dir = s->regs[G233_GPIO_DIR];
    uint32_t out = s->regs[G233_GPIO_OUT];
    uint32_t trig = s->regs[G233_GPIO_TRIG];
    uint32_t pol = s->regs[G233_GPIO_POL];
    uint32_t ie = s->regs[G233_GPIO_IE];

    out &= dir;
    s->regs[G233_GPIO_OUT] = out;

    uint32_t in = s->regs[G233_GPIO_IN];
    in &= ~dir;
    in |= (out & dir);
    s->regs[G233_GPIO_IN] = in;

    // 处理电平中断
    for (int i = 0; i < 32; i++) {
        if (!(trig & (1u << i))) {
            continue;
        }
        if (!(ie & (1u << i))) {
            s->regs[G233_GPIO_IS] &= ~(1u << i);
            continue;
        }
        if (((in >> i) & 1) == ((pol >> i) & 1)) {
            s->regs[G233_GPIO_IS] |= (1u << i);
        } else {
            s->regs[G233_GPIO_IS] &= ~(1u << i);
        }
    }

    g233_gpio_update_irq(s);
}

static uint64_t g233_gpio_read(void *opaque, hwaddr offset, unsigned int size)
{
    G233GPIOState *s = G233_GPIO(opaque);
    uint32_t idx = offset / 4;

    if (idx >= G233_GPIO_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }

    return s->regs[idx];
}

static void g233_gpio_write(void *opaque, hwaddr offset,
                             uint64_t value, unsigned int size)
{
    G233GPIOState *s = G233_GPIO(opaque);
    uint32_t idx = offset / 4;

    switch (idx) {
    case G233_GPIO_DIR:
        s->regs[G233_GPIO_DIR] = value;
        break;

    case G233_GPIO_OUT:
        uint32_t old_out = s->regs[G233_GPIO_OUT];
        s->regs[G233_GPIO_OUT] = value;

        uint32_t changed = old_out ^ value;
        uint32_t trig = s->regs[G233_GPIO_TRIG];
        uint32_t pol = s->regs[G233_GPIO_POL];
        uint32_t ie = s->regs[G233_GPIO_IE];
        // 处理边缘中断
        for (int i = 0; i < 32; i++) {
            if (!(ie & (1u << i))) {
                continue;
            }
            if (trig & (1u << i)) {
                continue;
            }
            if (!(changed & (1u << i))) {
                continue;
            }
            if (((value >> i) & 1) == ((pol >> i) & 1)) {
                s->regs[G233_GPIO_IS] |= (1u << i);
            }
        }
        break;

    case G233_GPIO_IN:
        /* Read-only */
        break;

    case G233_GPIO_IE:
        s->regs[G233_GPIO_IE] = value;
        break;

    case G233_GPIO_IS:
        /* Write 1 to clear */
        s->regs[G233_GPIO_IS] &= ~value;
        break;

    case G233_GPIO_TRIG:
        s->regs[G233_GPIO_TRIG] = value;
        break;

    case G233_GPIO_POL:
        s->regs[G233_GPIO_POL] = value;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    update_state(s);
}

static const MemoryRegionOps g233_gpio_ops = {
    .read = g233_gpio_read,
    .write = g233_gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void g233_gpio_reset(DeviceState *dev)
{
    G233GPIOState *s = G233_GPIO(dev);

    memset(s->regs, 0, sizeof(s->regs));
}

static void g233_gpio_init(Object *obj)
{
    G233GPIOState *s = G233_GPIO(obj);

    memory_region_init_io(&s->mmio, obj, &g233_gpio_ops, s,
                          TYPE_G233_GPIO, 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static const VMStateDescription vmstate_g233_gpio = {
    .name = TYPE_G233_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, G233GPIOState, G233_GPIO_NR_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static void g233_gpio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = NULL;
    device_class_set_legacy_reset(dc, g233_gpio_reset);
    dc->vmsd = &vmstate_g233_gpio;
}

static const TypeInfo g233_gpio_info = {
    .name = TYPE_G233_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233GPIOState),
    .instance_init = g233_gpio_init,
    .class_init = g233_gpio_class_init,
};

static void g233_gpio_register_types(void)
{
    type_register_static(&g233_gpio_info);
}

type_init(g233_gpio_register_types)

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/core/irq.h"
#include "hw/watchdog/g233_wdt.h"
#include "migration/vmstate.h"

#define WDT_TICK_NS 1000000ULL  /* 1ms */

static void g233_wdt_update(G233WDTState *s)
{
    bool timeout = (s->sr & WDT_SR_TIMEOUT) != 0;
    bool int_en = (s->ctrl & WDT_CTRL_INTEN) != 0;

    qemu_set_irq(s->irq, (timeout && int_en) ? 1 : 0);

    if ((s->ctrl & WDT_CTRL_EN) && s->val > 0) {
        if (!timer_pending(s->timer)) {
            timer_mod(s->timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + WDT_TICK_NS);
        }
    } else {
        timer_del(s->timer);
    }
}

static void g233_wdt_timer_cb(void *opaque)
{
    G233WDTState *s = G233_WDT(opaque);

    if (!(s->ctrl & WDT_CTRL_EN)) {
        return;
    }

    if (s->val > 0) {
        s->val--;
        if (s->val == 0) {
            s->sr |= WDT_SR_TIMEOUT;
        }
    }

    g233_wdt_update(s);
}

static uint64_t g233_wdt_read(void *opaque, hwaddr offset, unsigned int size)
{
    G233WDTState *s = G233_WDT(opaque);

    switch (offset) {
    case WDT_CTRL:
        return s->ctrl;
    case WDT_LOAD:
        return s->load;
    case WDT_VAL:
        return s->val;
    case WDT_SR:
        return s->sr;
    case WDT_KEY:
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }
}

static void g233_wdt_write(void *opaque, hwaddr offset,
                           uint64_t value, unsigned int size)
{
    G233WDTState *s = G233_WDT(opaque);

    switch (offset) {
    case WDT_CTRL:
        if (!s->lock) {
            uint32_t old_ctrl = s->ctrl;
            s->ctrl = value & WDT_CTRL_MASK;
            if ((s->ctrl & WDT_CTRL_EN) && !(old_ctrl & WDT_CTRL_EN)) {
                s->val = s->load;
            }
        }
        break;

    case WDT_LOAD:
        if (!s->lock) {
            s->load = value;
        }
        break;

    case WDT_VAL:
        break;

    case WDT_SR:
        s->sr &= ~value;
        break;

    case WDT_KEY:
        if (s->lock) {
            break;
        }
        if (value == WDT_KEY_FEED) {
            s->val = s->load;
            s->sr &= ~WDT_SR_TIMEOUT;
        } else if (value == WDT_KEY_LOCK) {
            s->lock = true;
        }
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return;
    }

    g233_wdt_update(s);
}

static const MemoryRegionOps g233_wdt_ops = {
    .read = g233_wdt_read,
    .write = g233_wdt_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void g233_wdt_reset(DeviceState *dev)
{
    G233WDTState *s = G233_WDT(dev);

    s->ctrl   = 0;
    s->load   = 0;
    s->val    = 0;
    s->sr     = 0;
    s->lock = false;
    timer_del(s->timer);
}

static void g233_wdt_init(Object *obj)
{
    G233WDTState *s = G233_WDT(obj);

    memory_region_init_io(&s->mmio, obj, &g233_wdt_ops, s,
                          TYPE_G233_WDT, 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static void g233_wdt_realize(DeviceState *dev, Error **errp)
{
    G233WDTState *s = G233_WDT(dev);

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, g233_wdt_timer_cb, s);
}

static const VMStateDescription vmstate_g233_wdt = {
    .name = TYPE_G233_WDT,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ctrl, G233WDTState),
        VMSTATE_UINT32(load, G233WDTState),
        VMSTATE_UINT32(val, G233WDTState),
        VMSTATE_UINT32(sr, G233WDTState),
        VMSTATE_BOOL(lock, G233WDTState),
        VMSTATE_END_OF_LIST()
    }
};

static void g233_wdt_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = g233_wdt_realize;
    device_class_set_legacy_reset(dc, g233_wdt_reset);
    dc->vmsd = &vmstate_g233_wdt;
}

static const TypeInfo g233_wdt_info = {
    .name = TYPE_G233_WDT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233WDTState),
    .instance_init = g233_wdt_init,
    .class_init = g233_wdt_class_init,
};

static void g233_wdt_register_types(void)
{
    type_register_static(&g233_wdt_info);
}

type_init(g233_wdt_register_types)

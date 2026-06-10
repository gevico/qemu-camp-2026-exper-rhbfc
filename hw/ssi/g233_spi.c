#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/ssi/ssi.h"
#include "hw/ssi/g233_spi.h"
#include "migration/vmstate.h"

static void g233_spi_update_irq(G233SPIState *s)
{
    uint32_t cr1 = s->regs[G233_SPI_CR1];
    uint32_t sr  = s->regs[G233_SPI_SR];
    bool pending = false;

    if ((cr1 & SPI_CR1_TXEIE) && (sr & SPI_SR_TXE)) {
        pending = true;
    }
    if ((cr1 & SPI_CR1_RXNEIE) && (sr & SPI_SR_RXNE)) {
        pending = true;
    }
    if ((cr1 & SPI_CR1_ERRIE) && (sr & SPI_SR_OVERRUN)) {
        pending = true;
    }

    qemu_set_irq(s->irq, pending ? 1 : 0);
}

static void g233_spi_update_cs(G233SPIState *s)
{
    uint32_t cs = s->regs[G233_SPI_CR2] & 0x3;

    for (int i = 0; i < G233_SPI_NUM_CS; i++) {
        qemu_set_irq(s->cs_lines[i], i == cs ? 0 : 1);
    }
}

static uint64_t g233_spi_read(void *opaque, hwaddr offset, unsigned int size)
{
    G233SPIState *s = G233_SPI(opaque);
    uint32_t idx = offset / 4;

    if (idx >= G233_SPI_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }

    switch (idx) {
    case G233_SPI_DR:
        s->regs[G233_SPI_SR] &= ~SPI_SR_RXNE;
        g233_spi_update_irq(s);
        return s->rx_data;

    default:
        return s->regs[idx];
    }
}

static void g233_spi_write(void *opaque, hwaddr offset,
                           uint64_t value, unsigned int size)
{
    G233SPIState *s = G233_SPI(opaque);
    uint32_t idx = offset / 4;

    if (idx >= G233_SPI_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return;
    }

    switch (idx) {
    case G233_SPI_CR1:
        s->regs[G233_SPI_CR1] = value;
        if (value & SPI_CR1_SPE) {
            s->regs[G233_SPI_SR] |= SPI_SR_TXE;
        } else {
            s->regs[G233_SPI_SR] &= ~(SPI_SR_TXE | SPI_SR_RXNE | SPI_SR_OVERRUN);
        }
        break;

    case G233_SPI_CR2:
        s->regs[G233_SPI_CR2] = value;
        g233_spi_update_cs(s);
        break;

    case G233_SPI_SR:
        s->regs[G233_SPI_SR] &= ~(value & SPI_SR_OVERRUN);
        break;

    case G233_SPI_DR:
        if (!(s->regs[G233_SPI_SR] & SPI_SR_TXE)) {
            break;
        }
        s->tx_data = value & 0xFF;
        s->rx_data = ssi_transfer(s->spi, s->tx_data);
        if (s->regs[G233_SPI_SR] & SPI_SR_RXNE) {
            s->regs[G233_SPI_SR] |= SPI_SR_OVERRUN;
        } else {
            s->regs[G233_SPI_SR] |= SPI_SR_RXNE;
        }
        break;
    }

    g233_spi_update_irq(s);
}

static const MemoryRegionOps g233_spi_ops = {
    .read = g233_spi_read,
    .write = g233_spi_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void g233_spi_reset(DeviceState *dev)
{
    G233SPIState *s = G233_SPI(dev);

    memset(s->regs, 0, sizeof(s->regs));
    s->tx_data = 0;
    s->rx_data = 0;
    qemu_set_irq(s->irq, 0);

    for (int i = 0; i < G233_SPI_NUM_CS; i++) {
        DeviceState *kid = ssi_get_cs(s->spi, i);
        if (kid) {
            qemu_irq cs_line = qdev_get_gpio_in_named(kid, SSI_GPIO_CS, 0);
            qdev_connect_gpio_out_named(DEVICE(s), "cs", i, cs_line);
        }
        qemu_set_irq(s->cs_lines[i], 1);
    }
}

static void g233_spi_init(Object *obj)
{
    G233SPIState *s = G233_SPI(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &g233_spi_ops, s,
                          TYPE_G233_SPI, 0x100);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
}

static void g233_spi_realize(DeviceState *dev, Error **errp)
{
    G233SPIState *s = G233_SPI(dev);

    s->spi = ssi_create_bus(dev, "spi");
    qdev_init_gpio_out_named(DEVICE(dev), s->cs_lines, "cs", G233_SPI_NUM_CS);
}

static const VMStateDescription vmstate_g233_spi = {
    .name = TYPE_G233_SPI,
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, G233SPIState, G233_SPI_NR_REGS),
        VMSTATE_UINT8(tx_data, G233SPIState),
        VMSTATE_UINT8(rx_data, G233SPIState),
        VMSTATE_END_OF_LIST()
    }
};

static void g233_spi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = g233_spi_realize;
    device_class_set_legacy_reset(dc, g233_spi_reset);
    dc->vmsd = &vmstate_g233_spi;
}

static const TypeInfo g233_spi_info = {
    .name           = TYPE_G233_SPI,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(G233SPIState),
    .instance_init  = g233_spi_init,
    .class_init     = g233_spi_class_init,
};

static void g233_spi_register_types(void)
{
    type_register_static(&g233_spi_info);
}

type_init(g233_spi_register_types)

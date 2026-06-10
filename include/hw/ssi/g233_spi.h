#ifndef HW_G233_SPI_H
#define HW_G233_SPI_H

#include "hw/core/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qom/object.h"

#define TYPE_G233_SPI "g233-spi"
OBJECT_DECLARE_SIMPLE_TYPE(G233SPIState, G233_SPI)

#define G233_SPI_NUM_CS   4
#define G233_SPI_NR_REGS  4

#define SPI_CR1_SPE     (1u << 0)
#define SPI_CR1_MSTR    (1u << 2)
#define SPI_CR1_ERRIE   (1u << 5)
#define SPI_CR1_RXNEIE  (1u << 6)
#define SPI_CR1_TXEIE   (1u << 7)

#define SPI_SR_RXNE     (1u << 0)
#define SPI_SR_TXE      (1u << 1)
#define SPI_SR_OVERRUN  (1u << 4)

struct G233SPIState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq irq;

    SSIBus *spi;
    qemu_irq cs_lines[G233_SPI_NUM_CS];

    uint32_t regs[G233_SPI_NR_REGS];
    uint8_t tx_data;
    uint8_t rx_data;
};

enum {
    G233_SPI_CR1 = 0,
    G233_SPI_CR2 = 1,
    G233_SPI_SR  = 2,
    G233_SPI_DR  = 3,
};


#endif /* HW_G233_SPI_H */

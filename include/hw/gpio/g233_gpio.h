#ifndef HW_G233_GPIO_H
#define HW_G233_GPIO_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_G233_GPIO "g233-gpio"
OBJECT_DECLARE_SIMPLE_TYPE(G233GPIOState, G233_GPIO)

#define G233_GPIO_NR_REGS 7

struct G233GPIOState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq irq;
    uint32_t regs[G233_GPIO_NR_REGS];
};

enum {
    G233_GPIO_DIR  = 0,
    G233_GPIO_OUT  = 1,
    G233_GPIO_IN   = 2,
    G233_GPIO_IE   = 3,
    G233_GPIO_IS   = 4,
    G233_GPIO_TRIG = 5,
    G233_GPIO_POL  = 6,
};

#endif

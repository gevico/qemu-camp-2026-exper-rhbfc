#ifndef HW_G233_WDT_H
#define HW_G233_WDT_H

#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "qemu/timer.h"

#define TYPE_G233_WDT "g233-wdt"
OBJECT_DECLARE_SIMPLE_TYPE(G233WDTState, G233_WDT)

#define WDT_CTRL  0x00
#define WDT_LOAD  0x04
#define WDT_VAL   0x08
#define WDT_SR    0x0C
#define WDT_KEY   0x10

#define WDT_CTRL_EN    (1u << 0)
#define WDT_CTRL_INTEN (1u << 1)
#define WDT_CTRL_MASK  0x3

#define WDT_KEY_FEED 0x5A5A5A5A
#define WDT_KEY_LOCK 0x1ACCE551

#define WDT_SR_TIMEOUT (1u << 0)

struct G233WDTState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    QEMUTimer *timer;

    uint32_t ctrl;
    uint32_t load;
    uint32_t val;
    uint32_t sr;
    bool lock;
};

#endif

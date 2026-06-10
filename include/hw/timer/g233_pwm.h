#ifndef HW_G233_PWM_H
#define HW_G233_PWM_H

#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "qemu/timer.h"

#define TYPE_G233_PWM "g233-pwm"
OBJECT_DECLARE_SIMPLE_TYPE(G233PWMState, G233_PWM)

#define G233_PWM_CHANNELS 4

#define PWM_GLB             0x00

#define PWM_CH_OFF_CTRL     0x00
#define PWM_CH_OFF_PERIOD   0x04
#define PWM_CH_OFF_DUTY     0x08
#define PWM_CH_OFF_CNT      0x0C

#define PWM_CH_CTRL(n)   (0x10 + (n) * 0x10 + PWM_CH_OFF_CTRL)
#define PWM_CH_PERIOD(n) (0x10 + (n) * 0x10 + PWM_CH_OFF_PERIOD)
#define PWM_CH_DUTY(n)   (0x10 + (n) * 0x10 + PWM_CH_OFF_DUTY)
#define PWM_CH_CNT(n)    (0x10 + (n) * 0x10 + PWM_CH_OFF_CNT)

#define PWM_GLB_CH_EN(n)    (1u << (n))
#define PWM_GLB_CH_DONE(n)  (1u << (4 + (n)))
#define PWM_GLB_CH_DONE_MASK 0xF0

#define PWM_CTRL_EN         (1u << 0)
#define PWM_CTRL_POL        (1u << 1)
#define PWM_CTRL_MASK       (PWM_CTRL_EN | PWM_CTRL_POL)

#define G233_PWM_FREQ_HZ 1000000ULL

typedef struct {
    uint32_t ctrl;
    uint32_t period;
    uint32_t duty;
    uint32_t cnt;
} G233PWMChannel;

struct G233PWMState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    QEMUTimer *timer;

    uint32_t glb;
    G233PWMChannel ch[G233_PWM_CHANNELS];
};

#endif

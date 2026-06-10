#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/timer/g233_pwm.h"

#include "migration/vmstate.h"

static void g233_pwm_update_timer(G233PWMState *s)
{
    if (s->glb & (PWM_GLB_CH_EN(0) | PWM_GLB_CH_EN(1) |
                  PWM_GLB_CH_EN(2) | PWM_GLB_CH_EN(3))) {
        if (!timer_pending(s->timer)) {
            timer_mod(s->timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + NANOSECONDS_PER_SECOND / G233_PWM_FREQ_HZ);
        }
    } else {
        timer_del(s->timer);
    }
}

static void g233_pwm_timer_cb(void *opaque)
{
    G233PWMState *s = G233_PWM(opaque);

    for (int i = 0; i < G233_PWM_CHANNELS; i++) {
        if (!(s->glb & PWM_GLB_CH_EN(i))) {
            continue;
        }

        s->ch[i].cnt++;

        if (s->ch[i].period > 0 && s->ch[i].cnt >= s->ch[i].period) {
            s->ch[i].cnt = 0;
            s->glb |= PWM_GLB_CH_DONE(i);
        }
    }

    g233_pwm_update_timer(s);
}

static uint64_t g233_pwm_read(void *opaque, hwaddr offset, unsigned int size)
{
    G233PWMState *s = G233_PWM(opaque);

    if (offset == PWM_GLB) {
        return s->glb;
    }

    if (offset >= PWM_CH_CTRL(0)) {
        uint32_t ch = (offset - PWM_CH_CTRL(0)) / 0x10;
        uint32_t reg = (offset - PWM_CH_CTRL(0)) % 0x10;

        if (ch >= G233_PWM_CHANNELS) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: bad channel %u\n", __func__, ch);
            return 0;
        }

        switch (reg) {
        case PWM_CH_OFF_CTRL:
            return s->ch[ch].ctrl;
        case PWM_CH_OFF_PERIOD:
            return s->ch[ch].period;
        case PWM_CH_OFF_DUTY:
            return s->ch[ch].duty;
        case PWM_CH_OFF_CNT:
            return s->ch[ch].cnt;
        }
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                  __func__, offset);
    return 0;
}

static void g233_pwm_write(void *opaque, hwaddr offset,
                           uint64_t value, unsigned int size)
{
    G233PWMState *s = G233_PWM(opaque);

    if (offset == PWM_GLB) {
        s->glb &= ~(value & PWM_GLB_CH_DONE_MASK);
        return;
    }

    if (offset >= PWM_CH_CTRL(0)) {
        unsigned int ch = (offset - PWM_CH_CTRL(0)) / 0x10;
        unsigned int reg = (offset - PWM_CH_CTRL(0)) % 0x10;

        if (ch >= G233_PWM_CHANNELS) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: bad channel %u\n", __func__, ch);
            return;
        }

        switch (reg) {
        case PWM_CH_OFF_CTRL:
            s->ch[ch].ctrl = value & PWM_CTRL_MASK;
            if (value & PWM_CTRL_EN) {
                s->glb |= PWM_GLB_CH_EN(ch);
            } else {
                s->glb &= ~PWM_GLB_CH_EN(ch);
            }

            g233_pwm_update_timer(s);
            return;

        case PWM_CH_OFF_PERIOD:
            s->ch[ch].period = value;
            return;

        case PWM_CH_OFF_DUTY:
            s->ch[ch].duty = value;
            return;
        }
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: bad write offset 0x%" HWADDR_PRIx "\n",
                  __func__, offset);
}

static const MemoryRegionOps g233_pwm_ops = {
    .read = g233_pwm_read,
    .write = g233_pwm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void g233_pwm_reset(DeviceState *dev)
{
    G233PWMState *s = G233_PWM(dev);

    s->glb = 0;
    memset(s->ch, 0, sizeof(s->ch));
    timer_del(s->timer);
}

static void g233_pwm_init(Object *obj)
{
    G233PWMState *s = G233_PWM(obj);

    memory_region_init_io(&s->mmio, obj, &g233_pwm_ops, s,
                          TYPE_G233_PWM, 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void g233_pwm_realize(DeviceState *dev, Error **errp)
{
    G233PWMState *s = G233_PWM(dev);

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, g233_pwm_timer_cb, s);
}


static const VMStateDescription vmstate_g233_pwm_ch = {
    .name = "g233-pwm-channel",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ctrl, G233PWMChannel),
        VMSTATE_UINT32(period, G233PWMChannel),
        VMSTATE_UINT32(duty, G233PWMChannel),
        VMSTATE_UINT32(cnt, G233PWMChannel),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_g233_pwm = {
    .name = TYPE_G233_PWM,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(glb, G233PWMState),
        VMSTATE_STRUCT_ARRAY(ch, G233PWMState, G233_PWM_CHANNELS, 0,
                             vmstate_g233_pwm_ch, G233PWMChannel),
        VMSTATE_END_OF_LIST()
    }
};

static void g233_pwm_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = g233_pwm_realize;
    device_class_set_legacy_reset(dc, g233_pwm_reset);
    dc->vmsd = &vmstate_g233_pwm;
}

static const TypeInfo g233_pwm_info = {
    .name = TYPE_G233_PWM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233PWMState),
    .instance_init = g233_pwm_init,
    .class_init = g233_pwm_class_init,
};

static void g233_pwm_register_types(void)
{
    type_register_static(&g233_pwm_info);
}

type_init(g233_pwm_register_types)

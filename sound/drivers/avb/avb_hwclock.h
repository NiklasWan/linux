#ifndef AVB_HWCLOCK_H
#define AVB_HWCLOCK_H
#include <linux/ktime.h>
#include <linux/ptp_clock_kernel.h>

#define REGISTER_AVB_CLOCK(register_func, unregister_func)  \
    struct avb_hwclock __avb_ptp_clock = { \
        .ptp_clock = { \
            .register_clock = (register_func), \
            .unregister_clock = (unregister_func), \
            }, \
    }

struct ptp_clock_instance {
    struct ptp_clock_info *__hw_clock;
    int (*register_clock) (struct net_device *net_dev);
    int (*unregister_clock) (struct ptp_clock_instance *ptp_clock_instance);
};

struct avb_hwclock {
    struct ptp_clock_instance ptp_clock;
};

extern struct avb_hwclock __avb_ptp_clock;

ktime_t get_avb_ptp_time(void);

#endif
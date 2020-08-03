#include "avb_hwclock.h"

#include <linux/time64.h>
#include <linux/printk.h>

ktime_t get_avb_ptp_time(void)
{
    struct timespec64 ts = {0};

    __avb_ptp_clock.ptp_clock.__hw_clock->gettimex64(__avb_ptp_clock.ptp_clock.__hw_clock, &ts, NULL);

    return timespec64_to_ktime(ts);
}
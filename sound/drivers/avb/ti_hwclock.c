#include <linux/netdevice.h>
#include <linux/phy.h>

#include "../../../drivers/net/ethernet/ti/cpts.h"
#include "../../../drivers/net/ethernet/ti/cpsw_priv.h"

#include "avb_hwclock.h"

int register_ti_clock(struct net_device *net_dev);
int unregister_ti_clock(struct ptp_clock_instance *ptp_clock_instance);

int register_ti_clock(struct net_device *net_dev)
{
    struct cpsw_common *cpsw;
    cpsw = ndev_to_cpsw(net_dev);

    __avb_ptp_clock.ptp_clock.__hw_clock = &cpsw->cpts->info;

    if (__avb_ptp_clock.ptp_clock.__hw_clock)
        return 0;
    else
        return -1;
}

int unregister_ti_clock(struct ptp_clock_instance *ptp_clock_instance)
{
    return 0;
}

REGISTER_AVB_CLOCK(register_ti_clock, unregister_ti_clock);

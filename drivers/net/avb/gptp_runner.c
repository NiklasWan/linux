/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * gPTP         An implementation of the gPTP protocol stack
 *              IEEE 802.1 AS-2020 for the Beaglebona Black/AI
 *
 * Authors:     Niklas Wantrupp
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include "gptp.h"
#include <linux/time.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Niklas Wantrupp");
MODULE_DESCRIPTION("A gPTP IEEE 802.1 AS-2020 kernel module");

// gptp helper functions 

static u64 calc_mean_dly(struct gptp_dly* dly_time)
{
    return (u64) (((double) dly_time->t_prop_initiator + dly_time->t_prop_responder) / 2);
}

static u64 calc_dly_asym_initiator(struct gptp_dly* dly_time, u64 mean_dly)
{
    return mean_dly - dly_time->t_prop_initiator;
}

static u64 calc_dly_asym_responder(struct gptp_dly* dly_time, u64 mean_dly)
{
    return dly_time->t_prop_responder - mean_dly;
}

static int octect_cmp(u8* a, u8* b, int len)
{
    int i = 0;

    for (;i < len; i++) {
        if (a[i] < b[i])
            return -1;
        else if (a[i] > b[i])
            return 1;
    }

    return 0;
}

static void calc_eui64(u8 *mac_buf, u8 *eui_buf)
{
    int i = 0;
    u8 mask = 0x02;
    eui_buf[3] = 0xFF;
    eui_buf[4] = 0xFE;

    for (i = 0; i < 3; i++) {
        eui_buf[i] = mac_buf[i];
    }

    for (i = 7; i > 4; i--) {
        eui_buf[i] = mac_buf[i-2];
    }

    eui_buf[0] ^= mask;
}

// kernel module constructor and destructor
static int __init gptp_init(void)
{
	printk(KERN_INFO "gPTP Init");

	return 0;
}

static void __exit gptp_exit(void)
{
	printk(KERN_INFO "gPTP Removed");
}

module_init(gptp_init);
module_exit(gptp_exit);
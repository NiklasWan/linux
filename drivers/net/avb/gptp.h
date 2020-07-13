/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * gPTP         An implementation of the gPTP protocol stack
 *              IEEE 802.1 AS-2020 for the Beaglebona Black/AI
 *
 * Authors:     Niklas Wantrupp
 */



#ifndef PTP_TYPES_H
#define PTP_TYPES_H

#include <linux/types.h>

#define BIT_MASK_4 0xF
#define BIT_MASK_8 0xFF
#define BIT_MASK_12 0xFFF
#define CLOCK_IDENT_OCTETS 8
#define MAJOR_SDO_ID 0x01 & BIT_MASK_4
#define MINOR_SDO_ID 0x00 & BIT_MASK_8

#define SDO_ID ((MAJOR_SDO_ID << 8) | MINOR_SDO_ID) & BIT_MASK_12

typedef s8 scaled_ns[12];
typedef u8 uscaled_ns[12];
typedef s64 time_interval;

enum ptp_instance_type {
    PTP_PRIO_1 = 0XF6, PTP_PRIO_2 = 0xF8, PTP_PRIO_3 = 0xFA, PTP_PRIO_4 = 0xFF
};

enum clock_source {
    ATOMIC_CLOCK = 0x10, GPS = 0x20, TERRESTRIAL_RADIO = 0x30, PTP = 0x40,  
    NTP = 0x50, HAND_SET = 0x60, OTHER = 0x90, INTERNAL_OSC = 0xA0
};

enum clock_accuracy {
    PS1 = 0x17, PS2_5, PS10, PS25, PS100, PS250, NS1, NS2_5, NS10, NS25, NS100, NS250,
    MUS1, MUS2_5, MUS10, MUS25, MUS100, MUS250, MS1, MS2_5, MS10, MS25, MS100, MS250,
    S1, S10, S, UNKWNOWN = 0xFE
};

struct timestamp_48B
{
    u16 msb;
    u32 lsb;
};


struct timestamp {
    struct timestamp_48B seconds;
    u32 nanoseconds;
};

struct extended_timestamp {
    struct timestamp_48B seconds;
    struct timestamp_48B fractional_nano_seconds;
};

struct port_identity {
    u8 clock_identity[CLOCK_IDENT_OCTETS];
    u16 port_number;
};

struct clock_quality {
    u8 clock_class;
    u8 clock_accuracy;
    u16 offset_scaled_log_variance;
};

struct gptp_domain {
    u8 domain_number;
    const u16 sdo_id :12;
} ptp_domain_init = {
    .domain_number = 0,
    .sdo_id = SDO_ID, 
};

struct gptp_dly {
    u64 t_prop_initiator;
    u64 t_prop_responder;
};

struct gptp_msg_class {
    int dummy :1;
};

struct gptp_msg_type {
    int dummy :1;
};

struct gptp_msg {
    struct gptp_msg_class class;
    struct gptp_msg_type type;
};

// gptp helper functions
static u64 calc_mean_dly(struct gptp_dly* dly_time);
static u64 calc_dly_asym_initiator(struct gptp_dly* dly_time, u64 mean_dly);
static u64 calc_dly_asym_responder(struct gptp_dly* dly_time, u64 mean_dly);
static int octect_cmp(u8* a, u8* b, int len);
static void calc_eui64(u8 *mac_buf, u8 *eui_buf);

#endif // PTP_TYPES_H

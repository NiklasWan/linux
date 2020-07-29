#ifndef GPTP_SYNC_H
#define GPTP_SYNC_H

#include "gptp_common.h"

#define CS_STATE_INIT                    0
#define CS_STATE_GRAND_MASTER            1
#define CS_STATE_SLAVE                   2

void init_cs(struct gptp_instance* gptp);
void deinit_cs(struct gptp_instance* gptp);
void cs_set_state(struct gptp_instance* gptp, bool gm_master);
void cs_handle_event(struct gptp_instance* gptp, int evt_id);

#ifdef CLOCK_SYNCHRONIZATION
static int adjust_ptp_time(struct ptp_clock_info *clock, struct __kernel_timex *tx);
static void cs_handle_state_change(struct gptp_instance* gptp, int to_state);
static void send_sync(struct gptp_instance* gptp);
static void send_sync_flwup(struct gptp_instance* gptp);
#endif

#endif

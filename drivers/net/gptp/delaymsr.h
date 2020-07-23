#ifndef GPTP_DELAY_MSR_H
#define GPTP_DELAY_MSR_H

#include "gptp_common.h"

#define DM_STATE_INIT			0
#define DM_STATE_IDLE			1
#define DM_STATE_DELAY_RESP_WAIT	2
#define DM_STATE_DELAY_RESP_FLWUP_WAIT	3

void init_dm(struct gptp_instance* gptp);
void deinit_dm(struct gptp_instance* gptp);
void dm_handle_event(struct gptp_instance* gptp, int evt_id);

#ifdef DELAY_MSR_MODULE
static void dm_handle_state_change(struct gptp_instance* gptp, int to_state);
static void send_delay_req(struct gptp_instance* gptp);
static void send_delay_resp(struct gptp_instance* gptp);
static void send_delay_resp_flwup(struct gptp_instance* gptp);
#endif

#endif

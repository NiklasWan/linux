#ifndef GPTP_BMC_H
#define GPTP_BMC_H

#include "gptp_common.h"

#define BMC_STATE_INIT                    0
#define BMC_STATE_GRAND_MASTER            1
#define BMC_STATE_SLAVE                   2

void init_bmc(struct gptp_instance* gptp);
void deinit_bmc(struct gptp_instance* gptp);
void bmc_handle_event(struct gptp_instance* gptp, int evt_id);

#ifdef BEST_MASTER_CLOCK_SELECTION
static void bmc_handle_state_change(struct gptp_instance* gptp, int to_state);
static void send_announce(struct gptp_instance* gptp);
static bool update_announce_info(struct gptp_instance* gptp);
static void update_prio_vectors(struct gptp_instance* gptp);
#endif

#endif

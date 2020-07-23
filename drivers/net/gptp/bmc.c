#define BEST_MASTER_CLOCK_SELECTION

#include "bmc.h"
#include "sync.h"

void init_bmc(struct gptp_instance* gptp)
{
	gptp->bmc.state = BMC_STATE_INIT;
	gptp->bmc.announce_interval = GPTP_ANNOUNCE_INTERVAL;
	gptp->bmc.announce_timeout  = GPTP_ANNOUNCE_TIMEOUT;
}

void deinit_bmc(struct gptp_instance* gptp)
{
	
}

void bmc_handle_event(struct gptp_instance* gptp, int evt_id)
{
	printk(KERN_INFO "gPTP bmc_handle_event st: %d evt: 0x%x \n", gptp->bmc.state, evt_id);
	
	switch(gptp->bmc.state) {

		case BMC_STATE_INIT:
			switch (evt_id) {
				case GPTP_EVT_STATE_ENTRY:
					break;
				case GPTP_EVT_BMC_ENABLE:
					bmc_handle_state_change(gptp, BMC_STATE_GRAND_MASTER);
					update_prio_vectors(gptp);
					break;
				case GPTP_EVT_STATE_EXIT:
					break;
				default:
					break;
			}
			break;

		case BMC_STATE_GRAND_MASTER:
			switch (evt_id) {
				case GPTP_EVT_STATE_ENTRY:
					gptp_start_timer(gptp, GPTP_TIMER_ANNOUNCE_RPT, gptp->bmc.announce_interval, GPTP_EVT_BMC_ANNOUNCE_RPT);
					cs_set_state(gptp, true);
					break;
				case GPTP_EVT_BMC_ANNOUNCE_MSG:
					if(update_announce_info(gptp) == true)
						bmc_handle_state_change(gptp, BMC_STATE_SLAVE);
					break;
				case GPTP_EVT_BMC_ANNOUNCE_RPT:
					send_announce(gptp);
					break;
				case GPTP_EVT_STATE_EXIT:
					gptp_stop_timer(gptp, GPTP_TIMER_ANNOUNCE_RPT);
					break;
				default:
					break;
			}
			break;

		case BMC_STATE_SLAVE:
			switch (evt_id) {
				case GPTP_EVT_STATE_ENTRY:
					gptp_start_timer(gptp, GPTP_TIMER_ANNOUNCE_TO, gptp->bmc.announce_timeout, GPTP_EVT_BMC_ANNOUNCE_TO);
					cs_set_state(gptp, false);
					break;
				case GPTP_EVT_BMC_ANNOUNCE_MSG:
					if(update_announce_info(gptp) == true)
						gptp_reset_timer(gptp, GPTP_TIMER_ANNOUNCE_TO);
					break;
				case GPTP_EVT_BMC_ANNOUNCE_TO:
					bmc_handle_state_change(gptp, BMC_STATE_GRAND_MASTER);
					break;
				case GPTP_EVT_STATE_EXIT:
					gptp_stop_timer(gptp, GPTP_TIMER_ANNOUNCE_TO);
					break;
				default:
					break;
			}
			break;

		default:
			break;
	}
}

static void bmc_handle_state_change(struct gptp_instance* gptp, int to_state)
{
	bmc_handle_event(gptp, GPTP_EVT_STATE_EXIT);
	gptp->bmc.state = to_state;
	bmc_handle_event(gptp, GPTP_EVT_STATE_ENTRY);
}

static void send_announce(struct gptp_instance* gptp)
{
	int err = 0;
	int tx_len = sizeof(struct ethhdr);
	struct gptp_hdr *gh = (struct gptp_hdr *)&gptp->sd->tx_buf[sizeof(struct ethhdr)];
	struct gptp_prio_vec *prio = (struct gptp_prio_vec*)&gptp->sd->tx_buf[GPTP_BODY_OFFSET];
	struct gptp_tlv *tlv;

	/* Fill gPTP header */
	gh->h.f.seq_no = gptp_chg_endianess_16(gptp->bmc.anno_seq_no);
	gh->h.f.b1.msg_type = (GPTP_TRANSPORT_L2 | GPTP_MSG_TYPE_ANNOUNCE);
	gh->h.f.flags = gptp_chg_endianess_16(GPTP_FLAGS_NONE);

	gh->h.f.ctrl = GPTP_CONTROL_DELAY_ANNOUNCE;
	gh->h.f.log_msg_int = gptp_calc_log_interval(gptp->bmc.announce_interval / 1000);

	/* Add gPTP header size */
	tx_len += sizeof(struct gptp_hdr);

	/* PTP body */
	memset(&gptp->sd->tx_buf[GPTP_BODY_OFFSET], 0, (GPTP_TX_BUF_SIZE - GPTP_BODY_OFFSET));
	prio->curr_UTC_off = gptp->bmc.port_prio.curr_UTC_off;
	prio->prio1 = gptp->bmc.port_prio.prio1;
	prio->clock_qual.clock_class = gptp->bmc.port_prio.clock_qual.clock_class;
	prio->clock_qual.clock_accuracy = gptp->bmc.port_prio.clock_qual.clock_accuracy;
	prio->clock_qual.offset_scaled_log_variance = gptp_chg_endianess_16(gptp->bmc.port_prio.clock_qual.offset_scaled_log_variance);
	prio->prio2 = gptp->bmc.port_prio.prio2;
	memcpy(&prio->iden[0], &gptp->bmc.port_prio.iden[0], GPTP_PORT_IDEN_LEN);
	prio->steps_rem = gptp->bmc.port_prio.steps_rem;
	prio->clock_src = gptp->bmc.port_prio.clock_src;
	tx_len += sizeof(struct gptp_prio_vec);

	/* Path trace TLV */
	tlv = (struct gptp_tlv *)&gptp->sd->tx_buf[tx_len];
	tlv->type = gptp_chg_endianess_16(GPTP_TLV_TYPE_PATH_TRACE);
	tlv->len  = gptp_chg_endianess_16(GPTP_CLOCK_IDEN_LEN);
	tx_len += sizeof(struct gptp_tlv);
	memcpy(&gptp->sd->tx_buf[tx_len], &gh->h.f.src_port_iden, GPTP_CLOCK_IDEN_LEN);
	tx_len += GPTP_CLOCK_IDEN_LEN;

	/* Insert length */
	gh->h.f.msg_len = gptp_chg_endianess_16(tx_len - sizeof(struct ethhdr));

	if ((err = gptp_send_msg(gptp, tx_len)) <= 0)
		printk(KERN_DEBUG "Announce Send failed %d\n", err);	
	else
		printk(KERN_INFO ">>> Announce (%d) sent\n", gptp->bmc.anno_seq_no++);
}

static bool update_announce_info(struct gptp_instance* gptp)
{
	bool gm_found = false;
	int i = 0;
	struct gptp_prio_vec *gn_prio = (struct gptp_prio_vec *)&gptp->sd->rx_buf[GPTP_BODY_OFFSET];

	printk(KERN_DEBUG "Gnprio: %x:%x:%x:%x:%x:%x:%x \n",
			    gn_prio->prio1, gn_prio->clock_qual.clock_class, gn_prio->clock_qual.clock_accuracy,
			    gptp_chg_endianess_16(gn_prio->clock_qual.offset_scaled_log_variance), gn_prio->prio2,
			    gn_prio->steps_rem, gn_prio->clock_src);
	printk(KERN_DEBUG "Gnprio: %x:%x:%x:%x:%x:%x:%x:%x \n",
			    gn_prio->iden[0], gn_prio->iden[1], gn_prio->iden[2], gn_prio->iden[3],
			    gn_prio->iden[4], gn_prio->iden[5], gn_prio->iden[6], gn_prio->iden[7]);
	printk(KERN_DEBUG "Portprio: %x:%x:%x:%x:%x:%x:%x \n",
			    gptp->bmc.port_prio.prio1, gptp->bmc.port_prio.clock_qual.clock_class,
			    gptp->bmc.port_prio.clock_qual.clock_accuracy,
			    gptp->bmc.port_prio.clock_qual.offset_scaled_log_variance,
			    gptp->bmc.port_prio.prio2,
			    gptp->bmc.port_prio.steps_rem,
			    gptp->bmc.port_prio.clock_src);
	printk(KERN_DEBUG "Portprio: %x:%x:%x:%x:%x:%x:%x:%x \n",
			    gptp->bmc.port_prio.iden[0], gptp->bmc.port_prio.iden[1],
			    gptp->bmc.port_prio.iden[2], gptp->bmc.port_prio.iden[3],
			    gptp->bmc.port_prio.iden[4], gptp->bmc.port_prio.iden[5],
			    gptp->bmc.port_prio.iden[6], gptp->bmc.port_prio.iden[7]);

	if(gn_prio->prio1 < gptp->bmc.port_prio.prio1)
		gm_found = true;
	else if(gn_prio->clock_qual.clock_class < gptp->bmc.port_prio.clock_qual.clock_class)
		gm_found = true;
	else if(gn_prio->clock_qual.clock_accuracy < gptp->bmc.port_prio.clock_qual.clock_accuracy)
		gm_found = true;
	else if(gptp_chg_endianess_16(gn_prio->clock_qual.offset_scaled_log_variance) < gptp->bmc.port_prio.clock_qual.offset_scaled_log_variance)
		gm_found = true;
	else if(gn_prio->prio2 < gptp->bmc.port_prio.prio2)
		gm_found = true;
	else if(gn_prio->steps_rem < gptp->bmc.port_prio.steps_rem)
		gm_found = true;
	else if(gn_prio->clock_src < gptp->bmc.port_prio.clock_src)
		gm_found = true;
	else {
		for(i = 0; ((i < GPTP_PORT_IDEN_LEN) && (gm_found == false)); i++) {
			if(gn_prio->iden[i] < gptp->bmc.port_prio.iden[i])
				gm_found = true;
			else if(gn_prio->iden[i] > gptp->bmc.port_prio.iden[i])
				break;
			else
				continue;	
		}	
	}

	if(gm_found == true) {
		printk(KERN_INFO "High prio announce from: %x:%x:%x:%x:%x:%x:%x:%x \n",
			    gn_prio->iden[0], gn_prio->iden[1], gn_prio->iden[2], gn_prio->iden[3],
			    gn_prio->iden[4], gn_prio->iden[5], gn_prio->iden[6], gn_prio->iden[7]);
		memcpy(&gptp->bmc.gm_prio, gn_prio, sizeof(struct gptp_prio_vec));
	} else {
		printk(KERN_INFO "Low prio announce from %x:%x:%x:%x:%x:%x:%x:%x \n",
			    gn_prio->iden[0], gn_prio->iden[1], gn_prio->iden[2], gn_prio->iden[3],
			    gn_prio->iden[4], gn_prio->iden[5], gn_prio->iden[6], gn_prio->iden[7]);
		memcpy(&gptp->bmc.gm_prio, &gptp->bmc.port_prio, sizeof(struct gptp_prio_vec));
	}
	
	return gm_found;
}
	
static void update_prio_vectors(struct gptp_instance* gptp)
{
	struct gptp_hdr *gh = (struct gptp_hdr *)&gptp->sd->tx_buf[sizeof(struct ethhdr)];

	gptp->bmc.port_prio.curr_UTC_off = 0;
	gptp->bmc.port_prio.prio1  = GPTP_DEFAULT_CLOCK_PRIO1;
	gptp->bmc.port_prio.clock_qual.clock_class = GPTP_DEFAULT_CLOCK_CLASS;
	gptp->bmc.port_prio.clock_qual.clock_accuracy = GPTP_DEFAULT_CLOCK_ACCURACY;
	gptp->bmc.port_prio.clock_qual.offset_scaled_log_variance = GPTP_DEFAULT_OFFSET_VARIANCE;
	gptp->bmc.port_prio.prio2 = GPTP_DEFAULT_CLOCK_PRIO2;
	memcpy(&gptp->bmc.port_prio.iden[0], &gh->h.f.src_port_iden[0], GPTP_PORT_IDEN_LEN);
	gptp->bmc.port_prio.steps_rem = GPTP_DEFAULT_STEPS_REMOVED;
	gptp->bmc.port_prio.clock_src = GPTP_CLOCK_TYPE_INT_OSC;

	memcpy(&gptp->bmc.gm_prio, &gptp->bmc.port_prio, sizeof(struct gptp_prio_vec));
}



#define DELAY_MSR_MODULE

#include <linux/printk.h>
#include <linux/time64.h>

#include "delaymsr.h"

void init_dm(struct gptp_instance* gptp)
{
	gptp->dm.state = DM_STATE_INIT;
	gptp->dm.delay_req_interval = GPTP_PDELAY_REQ_INTERVAL;
	gptp->dm.delay_req_timeout  = GPTP_PDELAY_REQ_TIMEOUT;
}

void deinit_dm(struct gptp_instance* gptp)
{
	
}

void dm_handle_event(struct gptp_instance* gptp, int evt_id)
{
	int i = 0;
	struct timespec64 diff[3];

	printk(KERN_INFO "gPTP dm_handle_event st: %d evt: 0x%x \n", gptp->dm.state, evt_id);
	
	switch(gptp->dm.state) {

		case DM_STATE_INIT:
			switch (evt_id) {
				case GPTP_EVT_STATE_ENTRY:
					break;
				case GPTP_EVT_DM_ENABLE:
					dm_handle_state_change(gptp, DM_STATE_IDLE);
					break;
				case GPTP_EVT_STATE_EXIT:
					break;
				default:
					break;
			}
			break;

		case DM_STATE_IDLE:
			switch (evt_id) {
				case GPTP_EVT_STATE_ENTRY:
					gptp_start_timer(gptp, GPTP_TIMER_DELAYREQ_RPT, gptp->dm.delay_req_interval, GPTP_EVT_DM_PDELAY_REQ_RPT);
					break;
				case GPTP_EVT_DM_PDELAY_REQ_RPT:
					gptp_stop_timer(gptp, GPTP_TIMER_DELAYREQ_RPT);
					memset(&gptp->ts[0], 0, sizeof(struct timespec64) * 4);
					send_delay_req(gptp);
					// get_tx_ts(gptp, &gptp->ts[0]);
					dm_handle_state_change(gptp, DM_STATE_DELAY_RESP_WAIT);
					break;
				case GPTP_EVT_DM_PDELAY_REQ:
					get_rx_ts(gptp, &gptp->ts[4]);
					send_delay_resp(gptp);
					// get_tx_ts(gptp, &gptp->ts[5]);
					send_delay_resp_flwup(gptp);
					break;
				case GPTP_EVT_STATE_EXIT:
					break;
				default:
					break;
			}
			break;

		case DM_STATE_DELAY_RESP_WAIT:
			switch (evt_id) {
				case GPTP_EVT_STATE_ENTRY:
					gptp_start_timer(gptp, GPTP_TIMER_DELAYREQ_TO, gptp->dm.delay_req_timeout, GPTP_EVT_DM_PDELAY_REQ_TO);
					break;
				case GPTP_EVT_DM_PDELAY_REQ:
					get_rx_ts(gptp, &gptp->ts[4]);
					send_delay_resp(gptp);
					// get_tx_ts(gptp, &gptp->ts[5]);
					send_delay_resp_flwup(gptp);
					break;
				case GPTP_EVT_DM_PDELAY_REQ_TO:
					memset(&gptp->ts[0], 0, sizeof(struct timespec64) * 4);
					send_delay_req(gptp);
					// get_tx_ts(gptp, &gptp->ts[0]);
					break;
				case GPTP_EVT_DM_PDELAY_RESP:
					gptp_stop_timer(gptp, GPTP_TIMER_DELAYREQ_TO);
					gptp_copy_ts_from_buf(&gptp->ts[1], &gptp->sd->rx_buf[GPTP_BODY_OFFSET]);
					get_rx_ts(gptp, &gptp->ts[3]);
					dm_handle_state_change(gptp, DM_STATE_DELAY_RESP_FLWUP_WAIT);
					break;
				case GPTP_EVT_STATE_EXIT:
					break;
				default:
					break;
			}
			break;

		case DM_STATE_DELAY_RESP_FLWUP_WAIT:
			switch (evt_id) {
				case GPTP_EVT_STATE_ENTRY:
					break;
				case GPTP_EVT_DM_PDELAY_REQ:
					get_rx_ts(gptp, &gptp->ts[4]);
					send_delay_resp(gptp);
					// get_tx_ts(gptp, &gptp->ts[5]);
					send_delay_resp_flwup(gptp);
					break;
				case GPTP_EVT_DM_PDELAY_RESP_FLWUP:
					gptp_copy_ts_from_buf(&gptp->ts[2], &gptp->sd->rx_buf[GPTP_BODY_OFFSET]);
					for(i = 0; i < 4; i++)
						printk(KERN_INFO "@@@ t%d: %lld_%ld\n", (i+1), (s64)gptp->ts[i].tv_sec, gptp->ts[i].tv_nsec);
					gptp_timespec_diff(&gptp->ts[0],&gptp->ts[3],&diff[0]);
					gptp_timespec_diff(&gptp->ts[1],&gptp->ts[2],&diff[1]);
					if(diff[1].tv_nsec > diff[0].tv_nsec) {
						printk(KERN_INFO "Negative delay ignored 0:%lu 1:%lu\n", diff[0].tv_nsec, diff[1].tv_nsec);
					} else {
						gptp_timespec_diff(&diff[1],&diff[0],&diff[2]);
						if(diff[2].tv_nsec > 50000) {
							printk(KERN_INFO "Abnormally large delay ignored %lu\n", (diff[2].tv_nsec / 2));
						} else {
							gptp->msrd_delay = diff[2].tv_nsec/ 2;
							printk(KERN_NOTICE "---> gPTP msrd_delay: %u\n", gptp->msrd_delay);
						}
					}
					dm_handle_state_change(gptp, DM_STATE_IDLE);
					break;
				case GPTP_EVT_STATE_EXIT:
					break;
				default:
					break;
			}
			break;

		default:
			break;
	}
}

static void dm_handle_state_change(struct gptp_instance* gptp, int to_state)
{
	dm_handle_event(gptp, GPTP_EVT_STATE_EXIT);
	gptp->dm.state = to_state;
	dm_handle_event(gptp, GPTP_EVT_STATE_ENTRY);
}

static void send_delay_req(struct gptp_instance* gptp)
{
	int err = 0;
	int tx_len = sizeof(struct ethhdr);
	struct gptp_hdr * gh = (struct gptp_hdr *)&gptp->sd->tx_buf[sizeof(struct ethhdr)];

	/* Fill gPTP header */
	gh->h.f.seq_no = gptp_chg_endianess_16(gptp->dm.tx_seq_no);
	gh->h.f.b1.msg_type = (GPTP_TRANSPORT_L2 | GPTP_MSG_TYPE_PDELAY_REQ);
	gh->h.f.flags = gptp_chg_endianess_16(GPTP_FLAGS_NONE);

	gh->h.f.ctrl = GPTP_CONTROL_DELAY_ANNOUNCE;
	gh->h.f.log_msg_int = gptp_calc_log_interval(gptp->dm.delay_req_interval / 1000);

	/* Add gPTP header size */
	tx_len += sizeof(struct gptp_hdr);

	/* PTP body */
	memset(&gptp->sd->tx_buf[GPTP_BODY_OFFSET], 0, (GPTP_TX_BUF_SIZE - GPTP_BODY_OFFSET));
	tx_len += (GPTP_TS_LEN + GPTP_PORT_IDEN_LEN);

	/* Insert length */
	gh->h.f.msg_len = gptp_chg_endianess_16(tx_len - sizeof(struct ethhdr));
	
	if ((err = gptp_send_msg(gptp, tx_len)) <= 0)
		printk(KERN_DEBUG "PDelayReq Send failed %d\n", err);
	else
		printk(KERN_INFO ">>> PDelayReq (%d) sent\n", gptp->dm.tx_seq_no++);
}

static void send_delay_resp(struct gptp_instance* gptp)
{
	int err = 0;
	int tx_len = sizeof(struct ethhdr);
	struct gptp_hdr * gh = (struct gptp_hdr *)&gptp->sd->tx_buf[sizeof(struct ethhdr)];

	/* Fill gPTP header */
	gh->h.f.seq_no = gptp_chg_endianess_16(gptp->dm.rx_seq_no);
	gh->h.f.b1.msg_type = (GPTP_TRANSPORT_L2 | GPTP_MSG_TYPE_PDELAY_RESP);
	gh->h.f.flags = gptp_chg_endianess_16(GPTP_FLAGS_TWO_STEP);

	gh->h.f.ctrl = GPTP_CONTROL_DELAY_ANNOUNCE;
	gh->h.f.log_msg_int = GPTP_LOG_MSG_INT_MAX;

	/* Add gPTP header size */
	tx_len += sizeof(struct gptp_hdr);

	/* PTP body */
	memset(&gptp->sd->tx_buf[GPTP_BODY_OFFSET], 0, (GPTP_TX_BUF_SIZE - GPTP_BODY_OFFSET));
	gptp_copy_ts_to_buf(&gptp->ts[4], &gptp->sd->tx_buf[tx_len]);
	tx_len += GPTP_TS_LEN;
	memcpy(&gptp->sd->tx_buf[tx_len], &gptp->dm.req_port_iden[0], GPTP_PORT_IDEN_LEN);
	tx_len += GPTP_PORT_IDEN_LEN;

	/* Insert length */
	gh->h.f.msg_len = gptp_chg_endianess_16(tx_len - sizeof(struct ethhdr));

	if ((err = gptp_send_msg(gptp, tx_len)) <= 0)
		printk(KERN_DEBUG "PDelayResp Send failed %d\n", err);	
	else
		printk(KERN_INFO "=== PDelayResp (%d) sent\n", gptp->dm.rx_seq_no);
}

static void send_delay_resp_flwup(struct gptp_instance* gptp)
{
	int err = 0;
	int tx_len = sizeof(struct ethhdr);
	struct gptp_hdr * gh = (struct gptp_hdr *)&gptp->sd->tx_buf[sizeof(struct ethhdr)];

	/* Fill gPTP header */
	gh->h.f.seq_no = gptp_chg_endianess_16(gptp->dm.rx_seq_no);
	gh->h.f.b1.msg_type = (GPTP_TRANSPORT_L2 | GPTP_MSG_TYPE_PDELAY_RESP_FLWUP);
	gh->h.f.flags = gptp_chg_endianess_16(GPTP_FLAGS_NONE);

	gh->h.f.ctrl = GPTP_CONTROL_DELAY_ANNOUNCE;
	gh->h.f.log_msg_int = GPTP_LOG_MSG_INT_MAX;

	/* Add gPTP header size */
	tx_len += sizeof(struct gptp_hdr);

	/* PTP body */
	memset(&gptp->sd->tx_buf[GPTP_BODY_OFFSET], 0, (GPTP_TX_BUF_SIZE - GPTP_BODY_OFFSET));
	gptp_copy_ts_to_buf(&gptp->ts[5], &gptp->sd->tx_buf[tx_len]);
	tx_len += GPTP_TS_LEN;
	memcpy(&gptp->sd->tx_buf[tx_len], &gptp->dm.req_port_iden[0], GPTP_PORT_IDEN_LEN);
	tx_len += GPTP_PORT_IDEN_LEN;

	/* Insert length */
	gh->h.f.msg_len = gptp_chg_endianess_16(tx_len - sizeof(struct ethhdr));

	if ((err = gptp_send_msg(gptp, tx_len) <= 0))
		printk(KERN_DEBUG "PDelayRespFlwUp Send failed %d\n", err);	
	else
		printk(KERN_INFO "=== PDelayRespFlwUp (%d) sent\n", gptp->dm.rx_seq_no);
}





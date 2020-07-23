#define CLOCK_SYNCHRONIZATION

#include <linux/printk.h>
#include <linux/time64.h>

#include "sync.h"

void init_cs(struct gptp_instance* gptp)
{
	gptp->cs.state = CS_STATE_INIT;
	gptp->cs.sync_interval = GPTP_SYNC_INTERVAL;
	gptp->cs.sync_timeout  = GPTP_SYNC_TIMEOUT;
}

void deinit_cs(struct gptp_instance* gptp)
{
	
}

void cs_set_state(struct gptp_instance* gptp, bool gm_master)
{
	if((gm_master == true) && (gptp->cs.state != CS_STATE_GRAND_MASTER)) {
		cs_handle_state_change(gptp, CS_STATE_GRAND_MASTER);
		printk(KERN_NOTICE "---> Assuming grandmaster role\n");
	} else if((gm_master == false) && (gptp->cs.state == CS_STATE_GRAND_MASTER)) {
		cs_handle_state_change(gptp, CS_STATE_SLAVE);
		printk(KERN_NOTICE "---> External grandmaster found\n");
	} else
		printk(KERN_WARNING "gPTP cannot set state st: %d gm_master: %d \n", gptp->cs.state, gm_master);	
}

void cs_handle_event(struct gptp_instance* gptp, int evt_id)
{
	int diffsign = 1;
	struct timespec64 sync[4];

	printk(KERN_INFO "gPTP csHandleEvent st: %d evt: 0x%x \n", gptp->cs.state, evt_id);
	
	switch(gptp->cs.state) {

		case CS_STATE_INIT:
			switch (evt_id) {
				case GPTP_EVT_STATE_ENTRY:
					break;
				case GPTP_EVT_CS_ENABLE:
					break;
				case GPTP_EVT_STATE_EXIT:
					break;
				default:
					break;
			}
			break;

		case CS_STATE_GRAND_MASTER:
			switch (evt_id) {
				case GPTP_EVT_STATE_ENTRY:
					gptp_start_timer(gptp, GPTP_TIMER_SYNC_RPT, gptp->cs.sync_interval, GPTP_EVT_CS_SYNC_RPT);
					break;
				case GPTP_EVT_CS_SYNC_RPT:
					send_sync(gptp);
					// get_tx_ts(gptp, &gptp->ts[6]);
					send_sync_flwup(gptp);
					break;
				case GPTP_EVT_STATE_EXIT:
					gptp_stop_timer(gptp, GPTP_TIMER_SYNC_RPT);
					break;
				default:
					break;
			}
			break;

		case CS_STATE_SLAVE:
			switch (evt_id) {
				case GPTP_EVT_STATE_ENTRY:
					gptp_start_timer(gptp, GPTP_TIMER_SYNC_TO, gptp->cs.sync_timeout, GPTP_EVT_CS_SYNC_TO);
					break;
				case GPTP_EVT_CS_SYNC_MSG:
					get_rx_ts(gptp, &gptp->ts[7]);
					break;
				case GPTP_EVT_CS_SYNC_FLWUP_MSG:
					gptp_copy_ts_from_buf(&gptp->ts[8], &gptp->sd->rx_buf[GPTP_BODY_OFFSET]);
					gptp->ts[9].tv_sec = 0;
					gptp->ts[9].tv_nsec = gptp->msrd_delay;

					gptp_timespec_sum(&gptp->ts[8],&gptp->ts[9],&sync[0]);
					diffsign = gptp_timespec_absdiff(&gptp->ts[7],&sync[0],&sync[1]);
					
					if(gptp->ptp_clock->gettime64(gptp->ptp_clock, &gptp->ts[10]) < 0)
						printk(KERN_ERR "clock_getTime failure\n");

					gptp_timespec_diff(&gptp->ts[7],&gptp->ts[10],&sync[2]);
					gptp_timespec_sum(&sync[2],&sync[0],&sync[3]);

					if((sync[1].tv_sec > 10) || (diffsign == -1)) {
						if(gptp->ptp_clock->settime64(gptp->ptp_clock, &sync[3]) < 0)
							printk(KERN_ERR "clock_setTime failure\n");
					} else {
						gptp->tx.time.tv_sec  = sync[1].tv_sec;
						gptp->tx.time.tv_usec = sync[1].tv_nsec;

						if(adjust_ptp_time(gptp->ptp_clock, &gptp->tx) < 0)
							printk(KERN_ERR "clock_adjTime failure\n");
					}
					
					if(gptp->ptp_clock->gettime64(gptp->ptp_clock, &gptp->ts[11]) < 0)
						printk(KERN_ERR "clock_getTime failure\n");					

					printk(KERN_INFO "@@@ SyncTxTime: %lld_%09ld\n", (s64)gptp->ts[8].tv_sec, gptp->ts[8].tv_nsec);
					printk(KERN_INFO "@@@ SyncRxTime: %lld_%09ld\n", (s64)gptp->ts[7].tv_sec, gptp->ts[7].tv_nsec);
					printk(KERN_INFO "@@@ lDelayTime: %lld_%09ld\n", (s64)gptp->ts[9].tv_sec, gptp->ts[9].tv_nsec);
					printk(KERN_INFO "@@@ CurrSynOff: %lld_%09ld (%d)\n", (s64)sync[1].tv_sec, sync[1].tv_nsec, diffsign);
					printk(KERN_INFO "@@@ prSyncTime: %lld_%09ld\n", (s64)gptp->ts[10].tv_sec, gptp->ts[10].tv_nsec);
					printk(KERN_NOTICE "@@@ poSyncTime: %lld_%09ld\n", (s64)gptp->ts[11].tv_sec, gptp->ts[11].tv_nsec);
					break;
				case GPTP_EVT_CS_SYNC_TO:
					break;
				case GPTP_EVT_STATE_EXIT:
					gptp_stop_timer(gptp, GPTP_TIMER_SYNC_TO);
					break;
				default:
					break;
			}
			break;

		default:
			break;
	}
}

static int adjust_ptp_time(struct ptp_clock_info *clock, struct __kernel_timex *tx) {
	int err = 0;
	if (tx->modes & ADJ_SETOFFSET) {
		struct timespec64 ts;
		ktime_t kt;
		s64 delta;

		ts.tv_sec  = tx->time.tv_sec;
		ts.tv_nsec = tx->time.tv_usec;

		if (!(tx->modes & ADJ_NANO))
			ts.tv_nsec *= 1000;

		if ((unsigned long) ts.tv_nsec >= NSEC_PER_SEC)
			return -EINVAL;

		kt = timespec64_to_ktime(ts);
		delta = ktime_to_ns(kt);
		err = clock->adjtime(clock, delta);
	} else if (tx->modes & ADJ_FREQUENCY) {
		s32 ppb = scaled_ppm_to_ppb(tx->freq);
		if (ppb > clock->max_adj || ppb < -clock->max_adj)
			return -ERANGE;
		if (clock->adjfine)
			err = clock->adjfine(clock, tx->freq);
		else
			err = clock->adjfreq(clock, ppb);
		// ptp->dialed_frequency = tx->freq;
	} /*else if (tx->modes == 0) {
		tx->freq = ptp->dialed_frequency;
		err = 0;
	}*/

	return err;
}

static void cs_handle_state_change(struct gptp_instance* gptp, int to_state)
{
	cs_handle_event(gptp, GPTP_EVT_STATE_EXIT);
	gptp->cs.state = to_state;
	cs_handle_event(gptp, GPTP_EVT_STATE_ENTRY);
}

static void send_sync(struct gptp_instance* gptp)
{
	int err = 0;
	int tx_len = sizeof(struct ethhdr);
	struct gptp_hdr *gh = (struct gptp_hdr *)&gptp->sd->tx_buf[sizeof(struct ethhdr)];
	struct kvec vec;

	/* Fill gPTP header */
	gh->h.f.seq_no = gptp_chg_endianess_16(gptp->cs.sync_seq_no);
	gh->h.f.b1.msg_type = (GPTP_TRANSPORT_L2 | GPTP_MSG_TYPE_SYNC);
	gh->h.f.flags = gptp_chg_endianess_16(GPTP_FLAGS_TWO_STEP);

	gh->h.f.ctrl = GPTP_CONTROL_SYNC;
	gh->h.f.log_msg_int = gptp_calc_log_interval(gptp->cs.sync_interval / 1000);

	/* Add gPTP header size */
	tx_len += sizeof(struct gptp_hdr);

	/* PTP body */
	memset(&gptp->sd->tx_buf[GPTP_BODY_OFFSET], 0, (GPTP_TX_BUF_SIZE - GPTP_BODY_OFFSET));
	tx_len += GPTP_TS_LEN;

	/* Insert length */
	gh->h.f.msg_len = gptp_chg_endianess_16(tx_len - sizeof(struct ethhdr));

	gptp->sd->txiov.iov_base = gptp->sd->tx_buf;
	gptp->sd->txiov.iov_len = GPTP_TX_BUF_SIZE;

	vec.iov_base = gptp->sd->txiov.iov_base;
	vec.iov_len = gptp->sd->txiov.iov_len;

	iov_iter_init(&gptp->sd->tx_msg_hdr.msg_iter, WRITE | ITER_KVEC, &gptp->sd->txiov, 1, GPTP_TX_BUF_SIZE);

	if ((err = gptp_send_msg(gptp, tx_len)) <= 0) {
		printk(KERN_DEBUG "Sync Send failed %d\n", err);	
		return;
	}

	printk(KERN_INFO ">>> Sync (%d) sent\n", gptp->cs.sync_seq_no);
}

static void send_sync_flwup(struct gptp_instance* gptp)
{
	int err = 0;
	int tx_len = sizeof(struct ethhdr);
	struct gptp_hdr *gh = (struct gptp_hdr *)&gptp->sd->tx_buf[sizeof(struct ethhdr)];
	struct gptp_tlv *tlv;
	struct gptp_org_ext *org_ext;

	/* Fill gPTP header */
	gh->h.f.seq_no = gptp_chg_endianess_16(gptp->cs.sync_seq_no);
	gh->h.f.b1.msg_type = (GPTP_TRANSPORT_L2 | GPTP_MSG_TYPE_SYNC_FLWUP);
	gh->h.f.flags = gptp_chg_endianess_16(GPTP_FLAGS_NONE);

	gh->h.f.ctrl = GPTP_CONTROL_SYNC_FLWUP;
	gh->h.f.log_msg_int = gptp_calc_log_interval(gptp->cs.sync_interval / 1000);

	/* Add gPTP header size */
	tx_len += sizeof(struct gptp_hdr);

	/* PTP body */
	memset(&gptp->sd->tx_buf[GPTP_BODY_OFFSET], 0, (GPTP_TX_BUF_SIZE - GPTP_BODY_OFFSET));
	gptp_copy_ts_to_buf(&gptp->ts[6], &gptp->sd->tx_buf[tx_len]);
	tx_len += GPTP_TS_LEN;

	/* Organization TLV */
	tlv = (struct gptp_tlv *)&gptp->sd->tx_buf[tx_len];
	tlv->type = gptp_chg_endianess_16(GPTP_TLV_TYPE_ORG_EXT);
	tlv->len  = gptp_chg_endianess_16(sizeof(struct gptp_org_ext));
	tx_len += sizeof(struct gptp_tlv);
	org_ext = (struct gptp_org_ext *)&gptp->sd->tx_buf[tx_len];
	org_ext->org_type[0] = 0x00; org_ext->org_type[1] = 0x80; org_ext->org_type[2] = 0xC2;
	org_ext->org_sub_type[0] = 0x00; org_ext->org_sub_type[1] = 0x00; org_ext->org_sub_type[2] = 0x01;
	tx_len += sizeof(struct gptp_org_ext);

	/* Insert length */
	gh->h.f.msg_len = gptp_chg_endianess_16(tx_len - sizeof(struct ethhdr));

	if ((err = gptp_send_msg(gptp, tx_len)) <= 0) {
		printk(KERN_ERR "SyncFollowup Send failed %d\n", err);		
		return;
	}

	printk(KERN_INFO "=== SyncFollowup (%d) sent\n", gptp->cs.sync_seq_no++);
}

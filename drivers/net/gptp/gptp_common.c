#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>
#include <linux/socket.h>
#include <net/sock.h>

#include "gptp_common.h"

void gptp_init_tx_buf(struct gptp_instance* gptp)
{
	struct ethhdr *eh = (struct ethhdr *) gptp->sd->tx_buf;
	struct gptp_hdr * gh = (struct gptp_hdr *) 
		&gptp->sd->tx_buf[sizeof(struct ethhdr)];

	/* Initialize it */
	memset(gptp->sd->tx_buf, 0, GPTP_TX_BUF_SIZE);

	/* Fill in the Ethernet header */
	
	memcpy(eh->h_dest, gptp->sd->destmac, 6);

	eh->h_source[0] = ((u8 *)&gptp->sd->srcmac)[0];
	eh->h_source[1] = ((u8 *)&gptp->sd->srcmac)[1];
	eh->h_source[2] = ((u8 *)&gptp->sd->srcmac)[2];
	eh->h_source[3] = ((u8 *)&gptp->sd->srcmac)[3];
	eh->h_source[4] = ((u8 *)&gptp->sd->srcmac)[4];
	eh->h_source[5] = ((u8 *)&gptp->sd->srcmac)[5];

	/* Fill in Ethertype field */
	eh->h_proto = htons(gptp->sd->type);

	/* Fill common gPTP header fields */
	gh->h.f.b1.ts_spec      = GPTP_TRANSPORT_L2;
	gh->h.f.b2.ptp_ver      = GPTP_VERSION_NO;
	gh->h.f.src_port_iden[0] = ((u8 *)&gptp->sd->srcmac)[0];
	gh->h.f.src_port_iden[1] = ((u8 *)&gptp->sd->srcmac)[1];
	gh->h.f.src_port_iden[2] = ((u8 *)&gptp->sd->srcmac)[2];
	gh->h.f.src_port_iden[3] = 0xFF;
	gh->h.f.src_port_iden[4] = 0xFE;
	gh->h.f.src_port_iden[5] = ((u8 *)&gptp->sd->srcmac)[3];
	gh->h.f.src_port_iden[6] = ((u8 *)&gptp->sd->srcmac)[4];
	gh->h.f.src_port_iden[7] = ((u8 *)&gptp->sd->srcmac)[5];
	gh->h.f.src_port_iden[8] = 0x00;
	gh->h.f.src_port_iden[9] = 0x01;
}

void gptp_init_rx_buf(struct gptp_instance* gptp)
{
	memset(gptp->sd->rx_buf, 0, GPTP_RX_BUF_SIZE);
	memset(gptp->sd->ts_buf, 0, GPTP_CON_TS_BUF_SIZE);
	gptp->sd->rxiov.iov_base = gptp->sd->rx_buf;
	gptp->sd->rxiov.iov_len  = GPTP_RX_BUF_SIZE;
	gptp->sd->rx_msg_hdr.msg_iocb = NULL;
	gptp->sd->rx_msg_hdr.msg_control = gptp->sd->ts_buf;
	gptp->sd->rx_msg_hdr.msg_controllen = sizeof(gptp->sd->ts_buf);
	gptp->sd->rx_msg_hdr.msg_flags = 0;
	gptp->sd->rx_msg_hdr.msg_name = &gptp->sd->rx_sock_address;
	gptp->sd->rx_msg_hdr.msg_namelen = sizeof(struct sockaddr_ll);
	iov_iter_init(&gptp->sd->rx_msg_hdr.msg_iter, READ, &gptp->sd->rxiov, 1, GPTP_RX_BUF_SIZE);	
}

u64 gptp_get_curr_milli_sec_ts(void)
{
 	u64 currTickTS = 0;
	struct timespec64 ts = {0};

	ktime_get_ts64(&ts);
	currTickTS = ((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));

	return currTickTS;
}

void gptp_start_timer(struct gptp_instance* gptp, u32 timer_id, 
		      u32 time_interval, u32 timer_evt)
{
 	gptp->timers[timer_id].time_interval = time_interval;
 	gptp->timers[timer_id].timer_evt = timer_evt;
 	gptp->timers[timer_id].last_ts = gptp_get_curr_milli_sec_ts();
}

void gptp_reset_timer(struct gptp_instance* gptp, u32 timer_id)
{
 	if(gptp->timers[timer_id].time_interval != 0)
 		gptp->timers[timer_id].last_ts = gptp_get_curr_milli_sec_ts();
}

void gptp_stop_timer(struct gptp_instance* gptp, u32 timer_id)
{
 	gptp->timers[timer_id].time_interval = 0;
 	gptp->timers[timer_id].timer_evt = GPTP_TIMER_INVALID;
 	gptp->timers[timer_id].last_ts = 0;
}


int gptp_timespec_absdiff(struct timespec64 *start, struct timespec64 *stop,
			  struct timespec64 *result)
{
	int diffsign = 1;

	if (stop->tv_sec > start->tv_sec) {
		gptp_timespec_diff(start, stop, result);
	} else if(stop->tv_sec < start->tv_sec) {
		gptp_timespec_diff(stop, start, result);
		diffsign = -1;
	} else if(stop->tv_nsec > start->tv_nsec) {
		gptp_timespec_diff(start, stop, result);
	} else {
		gptp_timespec_diff(stop, start, result);
		diffsign = -1;
	}
    
    return diffsign;
}

void gptp_timespec_diff(struct timespec64 *start, struct timespec64 *stop,
		       	struct timespec64 *result)
{
    if ((stop->tv_nsec - start->tv_nsec) < 0) {
        result->tv_sec = stop->tv_sec - start->tv_sec - 1;
        result->tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
    } else {
        result->tv_sec = stop->tv_sec - start->tv_sec;
        result->tv_nsec = stop->tv_nsec - start->tv_nsec;
    }

    return;
}

void gptp_timespec_sum(struct timespec64 *start, struct timespec64 *stop,
		       struct timespec64 *result)
{
    if ((stop->tv_nsec + start->tv_nsec) >= 1000000000) {
        result->tv_sec = stop->tv_sec + start->tv_sec + 1;
        result->tv_nsec = (stop->tv_nsec + start->tv_nsec) - 1000000000;
    } else {
        result->tv_sec = stop->tv_sec + start->tv_sec;
        result->tv_nsec = stop->tv_nsec + start->tv_nsec;
    }

    return;
}

void gptp_copy_ts_from_buf(struct timespec64 *ts, u8 *src)
{
	u8 *dest = (u8*)ts;
	struct gptp_ts *src_ts = (struct gptp_ts *)src;

	printk(KERN_DEBUG "fb: src  %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x \n",
		    src[0], src[1], src[2], src[3], src[4], src[5], src[6], src[7],
		    src[8], src[9], src[10], src[11]);

	if(sizeof(time_t) == 8) {
		ts->tv_sec  = (u64)(src_ts->s.lsb + ((u64)src_ts->s.msb << 32));
		ts->tv_nsec = src_ts->ns;
		printk(KERN_DEBUG "fb: dest %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x \n",
		    	    dest[0], dest[1], dest[2], dest[3], dest[4], dest[5], dest[6], dest[7],
		    	    dest[8], dest[9], dest[10], dest[11]);
	} else {
		ts->tv_sec  = src_ts->s.lsb;
		ts->tv_nsec = src_ts->ns;
		printk(KERN_DEBUG "fb: dest %02x %02x %02x %02x %02x %02x %02x %02x \n",
		   	    dest[0], dest[1], dest[2], dest[3], dest[4], dest[5], dest[6], dest[7]);
	}

	
}

void gptp_copy_ts_to_buf(struct timespec64 *ts, u8 *dest)
{
	u8 *src = (u8*)ts;
	struct gptp_ts *dest_ts = (struct gptp_ts *)dest;

	if(sizeof(time_t) == 8) {
		printk(KERN_DEBUG "tb: src  %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x \n",
		    	    src[0], src[1], src[2], src[3], src[4], src[5], src[6], src[7],
		    	    src[8], src[9], src[10], src[11]);
		dest_ts->s.msb = (u16)((u64)ts->tv_sec >> 32);
		dest_ts->s.lsb = (u32)ts->tv_sec;
		dest_ts->ns    = ts->tv_nsec;
	} else {
		printk(KERN_DEBUG "--@@-- tb: sec %lld, nsec: %ld\n", ts->tv_sec, ts->tv_nsec);
		printk(KERN_DEBUG "tb: src %02x %02x %02x %02x %02x %02x %02x %02x \n",
		    	    src[0], src[1], src[2], src[3], src[4], src[5], src[6], src[7]);
		dest_ts->s.msb = 0;
		dest_ts->s.lsb = ts->tv_sec;
		dest_ts->ns    = ts->tv_nsec;
	}

	printk(KERN_DEBUG "tb: dest  %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x \n",
		    dest[0], dest[1], dest[2], dest[3], dest[4], dest[5], dest[6], dest[7],
		    dest[8], dest[9]);
}

u16 gptp_chg_endianess_16(u16 val)
{
	return (((val & 0x00ff) << 8) | ((val & 0xff00) >> 8));
}

u8 gptp_calc_log_interval(u32 time)
{
	u8  log_int = 0;
	u32 lin_int = time;

	while(lin_int > 1) {
		lin_int = log_int >> 1;
		log_int++;
	}

	return log_int;
}

int gptp_send_msg(struct socketdata *sd, int tx_len)
{
	struct kvec vec;

	sd->txiov.iov_base = sd->tx_buf;
	sd->txiov.iov_len = tx_len;

	vec.iov_base = sd->txiov.iov_base;
	vec.iov_len = sd->txiov.iov_len;

	iov_iter_init(&sd->tx_msg_hdr.msg_iter, WRITE, &sd->txiov, 1, tx_len);

	return kernel_sendmsg(sd->sock, &sd->tx_msg_hdr, &vec, 1, tx_len);
}

void get_tx_ts(struct gptp_instance* gptp, struct timespec64* ts)
{
	int err = 0, level, type, i = 0;
	struct cmsghdr *cmsg = NULL;
	struct timespec *sw, *rts = NULL;
	struct kvec vec;
	mm_segment_t oldfs;
	int fd = -1;

	vec.iov_base = gptp->sd->rxiov.iov_base;
	vec.iov_len = gptp->sd->rxiov.iov_len;



	oldfs = get_fs();
	set_fs(KERNEL_DS);

	gptp_init_rx_buf(gptp);
	

	
	// err = sock_recv_errqueue(gptp->sd->sock->sk, &gptp->sd->rx_msg_hdr, 1, SOL_PACKET, PACKET_TX_TIMESTAMP);

	err =  kernel_recvmsg(gptp->sd->sock, &gptp->sd->rx_msg_hdr, &vec, 1, gptp->sd->rxiov.iov_len, MSG_ERRQUEUE);
	set_fs(oldfs);
	
	if (err <= 0) {
		printk(KERN_ERR "Recv failed %d\n", err);
		return;
	}
	
	printk(KERN_DEBUG "TxTs msg len: %d\n", err);
	cmsg = CMSG_FIRSTHDR( &gptp->sd->rx_msg_hdr);
	if (cmsg) {
		level = cmsg->cmsg_level;
		type  = cmsg->cmsg_type;
		printk(KERN_DEBUG "Lvl:%d Type: %d Size: %d\n", level, type, cmsg->cmsg_len);
	} else {
		printk(KERN_ERR "cmsghdr is not intact\n");
	}

	
	// for (cm = CMSG_FIRSTHDR(&gptp->sd->rx_msg_hdr); cm != NULL; cm = CMSG_NXTHDR(&gptp->sd->rx_msg_hdr, cm)) {
	// 	level = cm->cmsg_level;
	// 	type  = cm->cmsg_type;
	// 	printk(KERN_DEBUG "Lvl:%d Type: %d Size: %d (%d)\n", level, type, cm->cmsg_len, sizeof(struct timespec));
	// 	if (SOL_SOCKET == level && SO_TIMESTAMPING_NEW == type) {
	// 		if (cm->cmsg_len < sizeof(*ts) * 3) {
	// 			printk(KERN_DEBUG "short SO_TIMESTAMPING message\n");
	// 		} else {
	// 			rts = (struct timespec *) CMSG_DATA(cm);
	// 			for (i = 0; i < 3; i++)
	// 				if ((rts[i].tv_sec != 0) || (rts[i].tv_nsec != 0)) {
	// 					if (ts != NULL) {
	// 						ts->tv_sec =  rts[i].tv_sec;
	// 						ts->tv_nsec = rts[i].tv_nsec;
	// 					}							
	// 					printk(KERN_INFO "TxTS: %d: sec: %ld nsec: %ld \n", i, rts[i].tv_sec, rts[i].tv_nsec);
	// 				}
	// 		}
	// 	}
	// 	if (SOL_SOCKET == level && SO_TIMESTAMPNS_NEW == type) {
	// 		if (cm->cmsg_len < sizeof(*sw)) {
	// 			printk(KERN_DEBUG "short SO_TIMESTAMPNS message\n");
	// 		}
	// 	}
	// }
}


void get_rx_ts(struct gptp_instance* gptp, struct timespec64* ts)
{
	int level, type;
	struct cmsghdr *cm;
	struct timespec *sw, *rts = NULL;
	int i = 0;
	printk(KERN_DEBUG "We are retrieving rx timestamp\n");
	for (cm = CMSG_FIRSTHDR(&gptp->sd->rx_msg_hdr); cm != NULL; cm = CMSG_NXTHDR(&gptp->sd->rx_msg_hdr, cm)) {
		level = cm->cmsg_level;
		type  = cm->cmsg_type;
		printk(KERN_DEBUG "Lvl:%d Type: %d Size: %d (%d)\n", level, type, cm->cmsg_len, sizeof(struct timespec));
		if (SOL_SOCKET == level && SO_TIMESTAMPING_NEW == type) {
			if (cm->cmsg_len < sizeof(*ts) * 3) {
				printk(KERN_DEBUG "short SO_TIMESTAMPING_OLD message");
			} else {
				rts = (struct timespec *) CMSG_DATA(cm);
				for (i = 0; i < 3; i++)
					if ((rts[i].tv_sec != 0) || (rts[i].tv_nsec != 0)) {
						if (ts != NULL) {
							ts->tv_sec = rts[i].tv_sec;
							ts->tv_nsec = rts[i].tv_nsec;
						}
						printk(KERN_INFO "RxTS: %d: sec: %ld nsec: %ld \n", i, rts[i].tv_sec, rts[i].tv_nsec);
					}
			}
		}
		if (SOL_SOCKET == level && SO_TIMESTAMPNS_NEW == type) {
			if (cm->cmsg_len < sizeof(*sw)) {
				printk(KERN_DEBUG "short SO_TIMESTAMPNS message");
			}
		}
	}
}

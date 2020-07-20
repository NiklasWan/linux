#include <linux/kernel.h>
#include <linux/printk.h>
#include "gptp_common.h"

void gptp_init_tx_buf(struct gptp_instance* gptp)
{
	struct ethhdr *eh = (struct ethhdr *)gptp->txBuf;
	struct gPTPHdr * gh = (struct gPTPHdr *)&gptp->txBuf[sizeof(struct ethhdr)];

	/* Initialize it */
	memset(gptp->txBuf, 0, GPTP_TX_BUF_SIZE);

	/* Fill in the Ethernet header */
	eh->h_dest[0] = 0x01;
	eh->h_dest[1] = 0x80;
	eh->h_dest[2] = 0xC2;
	eh->h_dest[3] = 0x00;
	eh->h_dest[4] = 0x00;
	eh->h_dest[5] = 0x0E;
	eh->h_source[0] = ((u8 *)&gptp->if_mac.ifr_hwaddr.sa_data)[0];
	eh->h_source[1] = ((u8 *)&gptp->if_mac.ifr_hwaddr.sa_data)[1];
	eh->h_source[2] = ((u8 *)&gptp->if_mac.ifr_hwaddr.sa_data)[2];
	eh->h_source[3] = ((u8 *)&gptp->if_mac.ifr_hwaddr.sa_data)[3];
	eh->h_source[4] = ((u8 *)&gptp->if_mac.ifr_hwaddr.sa_data)[4];
	eh->h_source[5] = ((u8 *)&gptp->if_mac.ifr_hwaddr.sa_data)[5];

	/* Fill in Ethertype field */
	eh->h_proto = htons(ETH_P_1588);

	/* Fill common gPTP header fields */
	gh->h.f.b1.tsSpec      = GPTP_TRANSPORT_L2;
	gh->h.f.b2.ptpVer      = GPTP_VERSION_NO;
	gh->h.f.srcPortIden[0] = ((u8 *)&gptp->if_mac.ifr_hwaddr.sa_data)[0];
	gh->h.f.srcPortIden[1] = ((u8 *)&gptp->if_mac.ifr_hwaddr.sa_data)[1];
	gh->h.f.srcPortIden[2] = ((u8 *)&gptp->if_mac.ifr_hwaddr.sa_data)[2];
	gh->h.f.srcPortIden[3] = 0xFF;
	gh->h.f.srcPortIden[4] = 0xFE;
	gh->h.f.srcPortIden[5] = ((u8 *)&gptp->if_mac.ifr_hwaddr.sa_data)[3];
	gh->h.f.srcPortIden[6] = ((u8 *)&gptp->if_mac.ifr_hwaddr.sa_data)[4];
	gh->h.f.srcPortIden[7] = ((u8 *)&gptp->if_mac.ifr_hwaddr.sa_data)[5];
	gh->h.f.srcPortIden[8] = 0x00;
	gh->h.f.srcPortIden[9] = 0x01;
}

void gptp_init_rx_buf(struct gptp_instance* gptp)
{
	memset(gptp->rxBuf, 0, GPTP_RX_BUF_SIZE);
	memset(gptp->tsBuf, 0, GPTP_CON_TS_BUF_SIZE);
	gptp->rxMsgHdr.msg_iov = &gptp->rxiov;
	gptp->rxMsgHdr.msg_iovlen = 1;
	gptp->rxMsgHdr.msg_control=gptp->tsBuf;
	gptp->rxMsgHdr.msg_controllen=GPTP_CON_TS_BUF_SIZE;
	gptp->rxMsgHdr.msg_flags=0;
	gptp->rxMsgHdr.msg_name=&gptp->rxSockAddress;
	gptp->rxMsgHdr.msg_namelen=sizeof(struct sockaddr_ll);
}

u64 gptp_get_curr_milli_sec_ts(void)
{
	u64 currTickTS = 0;
	struct timespec ts = {0};

	clock_gettime(CLOCK_MONOTONIC, &ts);
	currTickTS = ((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));

	return currTickTS;
}

void gptp_start_timer(struct gptp_instance* gptp, u32 timerId, u32 timeInterval, u32 timerEvt)
{
	gptp->timers[timerId].timeInterval = timeInterval;
	gptp->timers[timerId].timerEvt = timerEvt;
	gptp->timers[timerId].lastTS = gptp_get_curr_milli_sec_ts();
}

void gptp_reset_timer(struct gptp_instance* gptp, u32 timerId)
{
	if(gptp->timers[timerId].timeInterval != 0)
		gptp->timers[timerId].lastTS = gptp_get_curr_milli_sec_ts();
}

void gptp_stop_timer(struct gptp_instance* gptp, u32 timerId)
{
	gptp->timers[timerId].timeInterval = 0;
	gptp->timers[timerId].timerEvt = GPTP_TIMER_INVALID;
	gptp->timers[timerId].lastTS = 0;
}


int gptp_timespec_absdiff(struct timespec *start, struct timespec *stop, struct timespec *result)
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

void gptp_timespec_diff(struct timespec *start, struct timespec *stop, struct timespec *result)
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

void gptp_timespec_sum(struct timespec *start, struct timespec *stop, struct timespec *result)
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

void gptp_copy_ts_from_buf(struct timespec *ts, u8 *src)
{
	u8 *dest = (u8*)ts;
	struct gPTPTS *srcTS = (struct gPTPTS *)src;

	printk(KERN_DEBUG "fb: src  %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x \n",
		    src[0], src[1], src[2], src[3], src[4], src[5], src[6], src[7],
		    src[8], src[9], src[10], src[11]);

	if(sizeof(time_t) == 8) {
		ts->tv_sec  = (u64)(srcTS->s.lsb + ((u64)srcTS->s.msb << 32));
		ts->tv_nsec = srcTS->ns;
		printk(KERN_DEBUG "fb: dest %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x \n",
		    	    dest[0], dest[1], dest[2], dest[3], dest[4], dest[5], dest[6], dest[7],
		    	    dest[8], dest[9], dest[10], dest[11]);
	} else {
		ts->tv_sec  = srcTS->s.lsb;
		ts->tv_nsec = srcTS->ns;
		printk(KERN_DEBUG "fb: dest %02x %02x %02x %02x %02x %02x %02x %02x \n",
		   	    dest[0], dest[1], dest[2], dest[3], dest[4], dest[5], dest[6], dest[7]);
	}

	
}

void gptp_copy_ts_to_buf(struct timespec *ts, u8 *dest)
{
	u8 *src = (u8*)ts;
	struct gPTPTS *destTS = (struct gPTPTS *)dest;

	if(sizeof(time_t) == 8) {
		printk(KERN_DEBUG "tb: src  %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x \n",
		    	    src[0], src[1], src[2], src[3], src[4], src[5], src[6], src[7],
		    	    src[8], src[9], src[10], src[11]);
		destTS->s.msb = (u16)((u64)ts->tv_sec >> 32);
		destTS->s.lsb = (u32)ts->tv_sec;
		destTS->ns    = ts->tv_nsec;
	} else {

		printk(KERN_DEBUG "tb: src %02x %02x %02x %02x %02x %02x %02x %02x \n",
		    	    src[0], src[1], src[2], src[3], src[4], src[5], src[6], src[7]);
		destTS->s.msb = 0;
		destTS->s.lsb = ts->tv_sec;
		destTS->ns    = ts->tv_nsec;
	}

	printk(KERN_DEBUG "tb: dest  %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x \n",
		    dest[0], dest[1], dest[2], dest[3], dest[4], dest[5], dest[6], dest[7],
		    dest[8], dest[9]);
}

u16 gptp_chg_endianess_16(u16 val)
{
	return (((val & 0x00ff) << 8) | ((val & 0xff00) >> 8));
}

u8 gptp_calcLogInterval(u32 time)
{
	u8  logInt = 0;
	u32 linInt = time;

	while(linInt > 1) {
		linInt = logInt >> 1;
		logInt++;
	}

	return logInt;
}


void get_tx_ts(struct gptp_instance* gptp, struct timespec* ts)
{
	static short sk_events = POLLPRI;
	static short sk_revents = POLLPRI;
	int cnt = 0, res = 0, level, type;
	struct cmsghdr *cm;
	struct timespec *sw, *rts = NULL;
	struct pollfd pfd = { gptp->sockfd, sk_events, 0 };

	do {
		res = poll(&pfd, 1, 1000);
		if (res < 1) {
			printk(KERN_DEBUG "Poll failed %d\n", res);
			break;
		} else if (!(pfd.revents & sk_revents)) {
			printk(KERN_ERR "poll for tx timestamp woke up on non ERR event");
			break;
		} else {
			printk(KERN_DEBUG "Poll success\n");
			gptp_init_rx_buf(gptp);
			cnt = recvmsg(gptp->sockfd, &gptp->rxMsgHdr, MSG_ERRQUEUE);
			if (cnt < 1)
				printk(KERN_ERR "Recv failed\n");
			else
				printk(KERN_DEBUG "TxTs msg len: %d\n", cnt);
				for (cm = CMSG_FIRSTHDR(&gptp->rxMsgHdr); cm != NULL; cm = CMSG_NXTHDR(&gptp->rxMsgHdr, cm)) {
					level = cm->cmsg_level;
					type  = cm->cmsg_type;
					printk(KERN_DEBUG "Lvl:%d Type: %d Size: %d (%d)\n", level, type, cm->cmsg_len, sizeof(struct timespec));
					if (SOL_SOCKET == level && SO_TIMESTAMPING == type) {
						if (cm->cmsg_len < sizeof(*ts) * 3) {
							printk(KERN_DEBUG "short SO_TIMESTAMPING message");
						} else {
							rts = (struct timespec *) CMSG_DATA(cm);
							for(int i = 0; i < 3; i++)
								if((rts[i].tv_sec != 0) || (rts[i].tv_nsec != 0)) {
									if(ts != NULL) {
                                                                                ts->tv_sec =  rts[i].tv_sec;
										ts->tv_nsec = rts[i].tv_nsec;
									}							
									printk(KERN_INFO "TxTS: %d: sec: %d nsec: %d \n", i, rts[i].tv_sec, rts[i].tv_nsec);
								}
						}
					}
					if (SOL_SOCKET == level && SO_TIMESTAMPNS == type) {
						if (cm->cmsg_len < sizeof(*sw)) {
							printk(KERN_DEBUG "short SO_TIMESTAMPNS message");
						}
					}
				}
		}
	} while(1);
}


void get_rx_ts(struct gptp_instance* gptp, struct timespec* ts)
{
	int level, type;
	struct cmsghdr *cm;
	struct timespec *sw, *rts = NULL;

	for (cm = CMSG_FIRSTHDR(&gptp->rxMsgHdr); cm != NULL; cm = CMSG_NXTHDR(&gptp->rxMsgHdr, cm)) {
		level = cm->cmsg_level;
		type  = cm->cmsg_type;
		printk(KERN_DEBUG "Lvl:%d Type: %d Size: %d (%d)\n", level, type, cm->cmsg_len, sizeof(struct timespec));
		if (SOL_SOCKET == level && SO_TIMESTAMPING == type) {
			if (cm->cmsg_len < sizeof(*ts) * 3) {
				printk(KERN_DEBUG "short SO_TIMESTAMPING message");
			} else {
				rts = (struct timespec *) CMSG_DATA(cm);
				for(int i = 0; i < 3; i++)
					if((rts[i].tv_sec != 0) || (rts[i].tv_nsec != 0)) {
						if(ts != NULL) {
							ts->tv_sec = rts[i].tv_sec;
							ts->tv_nsec = rts[i].tv_nsec;
						}
						printk(KERN_INFO "RxTS: %d: sec: %d nsec: %d \n", i, rts[i].tv_sec, rts[i].tv_nsec);
					}
			}
		}
		if (SOL_SOCKET == level && SO_TIMESTAMPNS == type) {
			if (cm->cmsg_len < sizeof(*sw)) {
				printk(KERN_DEBUG "short SO_TIMESTAMPNS message");
			}
		}
	}
}


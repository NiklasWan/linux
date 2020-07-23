#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <net/sock.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/phy.h>

#include "gptp_common.h"
#include "sync.h"
#include "delaymsr.h"

#include "../ethernet/ti/cpts.h"
#include "../ethernet/ti/cpsw_priv.h"

#define TIMEOUT 1000 //ms

MODULE_DESCRIPTION("gPTP kernel module");
MODULE_AUTHOR("Niklas Wantrupp <niklaswantrupp@web.de>");
MODULE_LICENSE("GPL");

void gptp_timer_callback(struct timer_list *data);
void gptp_worker_fn(struct work_struct *work);
int gptp_sock_init(void);

static struct timer_list gptp_callback_timer;
static struct workqueue_struct *gptp_workqueue;
static DECLARE_WORK(gptp_work, gptp_worker_fn);
static struct gptp_instance gptp;

static int gptp_parse_msg(void)
{
	int evt = GPTP_EVT_NONE;
	struct ethhdr * eh = (struct ethhdr *)&gptp.sd->rx_buf[0];
	struct gptp_hdr * gh = (struct gptp_hdr *)&gptp.sd->rx_buf[sizeof(struct ethhdr)];

	if(eh->h_proto == htons(ETH_P_1588)) {
		switch(gh->h.f.b1.msg_type & 0x0f)
		{
			case GPTP_MSG_TYPE_PDELAY_REQ:
				gptp.dm.rx_seq_no = gptp_chg_endianess_16(gh->h.f.seq_no);
				memcpy(&gptp.dm.req_port_iden[0], &gh->h.f.src_port_iden[0], GPTP_PORT_IDEN_LEN);
				printk(KERN_INFO "gPTP PDelayReq (%d) Rcvd \n", gptp.dm.rx_seq_no);
				evt = GPTP_EVT_DM_PDELAY_REQ;
				break;
			case GPTP_MSG_TYPE_PDELAY_RESP:
				printk(KERN_INFO "gPTP PDelayResp (%d) Rcvd \n", gptp_chg_endianess_16(gh->h.f.seq_no));
				evt = GPTP_EVT_DM_PDELAY_RESP;
				break;
			case GPTP_MSG_TYPE_PDELAY_RESP_FLWUP:
				printk(KERN_INFO "gPTP PDelayRespFlwUp (%d) Rcvd \n", gptp_chg_endianess_16(gh->h.f.seq_no));
				evt = GPTP_EVT_DM_PDELAY_RESP_FLWUP;
				break;
			case GPTP_MSG_TYPE_ANNOUNCE:
				printk(KERN_INFO "gPTP Announce (%d) Rcvd \n", gptp_chg_endianess_16(gh->h.f.seq_no));
				evt = GPTP_EVT_BMC_ANNOUNCE_MSG;
				break;
			case GPTP_MSG_TYPE_SYNC:
				printk(KERN_INFO "gPTP Sync (%d) Rcvd \n", gptp_chg_endianess_16(gh->h.f.seq_no));
				evt = GPTP_EVT_CS_SYNC_MSG;
				break;
			case GPTP_MSG_TYPE_SYNC_FLWUP:
				printk(KERN_INFO "gPTP SyncFlwUp (%d) Rcvd \n", gptp_chg_endianess_16(gh->h.f.seq_no));
				evt = GPTP_EVT_CS_SYNC_FLWUP_MSG;
				break;
			default:
				break;
		}
	};

	printk(KERN_DEBUG "gPTP parseMsg %d 0x%x 0x%x\n", eh->h_proto, gh->h.f.b1.msg_type, evt);

	return evt;
}

static void gptp_handle_event(int evt)
{
	/* Handle the events when available */
	if (evt != GPTP_EVT_NONE) {
		switch(evt & GPTP_EVT_DEST_MASK) {
			case GPTP_EVT_DEST_DM:
				dm_handle_event(&gptp, evt);
				break;
			case GPTP_EVT_DEST_BMC:
				//bmcHandleEvent(&gptp, evt);
				break;
			case GPTP_EVT_DEST_CS:
				cs_handle_event(&gptp, evt);
				break;
			default:
				printk(KERN_ERR "gPTP unknown evt 0x%x\n", evt);
				break;
		}
	}
}

void gptp_timer_callback(struct timer_list *data)
{
	int err = 0, i = 0;
	int evt = GPTP_EVT_NONE;
	u64 curr_tick_ts = gptp_get_curr_milli_sec_ts();
	struct kvec vec;
	mm_segment_t oldfs;

	/* Initialize tx */
	gptp_init_tx_buf(&gptp);

	/* Start the state machines */
	dm_handle_event(&gptp, GPTP_EVT_DM_ENABLE);
	// bmcHandleEvent(&gptp, GPTP_EVT_BMC_ENABLE);
	cs_handle_event(&gptp, GPTP_EVT_CS_ENABLE);

	/* Check for any timer event */
	for(i = 0; i < GPTP_NUM_TIMERS; i++) {
		if (gptp.timers[i].time_interval > 0) {
			printk(KERN_DEBUG "gPTP timer %d timeInt %lu timeEvt\
			       %d diffTS %ld\n", i, 
			       gptp.timers[i].time_interval, 
			       gptp.timers[i].timer_evt, 
			       (curr_tick_ts - gptp.timers[i].last_ts));
			/* When the requested time elapsed for this timer */
			if((gptp.timers[i].last_ts + 
			    gptp.timers[i].time_interval) < curr_tick_ts)			
			{
				/* Update and handle the timer event */
				gptp.timers[i].last_ts = curr_tick_ts;
				gptp_handle_event(gptp.timers[i].timer_evt);
			}
		}
	}

	/* Wait for GPTP events/messages */
	gptp_init_rx_buf(&gptp);
	gptp.sd->rxiov.iov_base = gptp.sd->rx_buf;
	gptp.sd->rxiov.iov_len = GPTP_RX_BUF_SIZE;
	vec.iov_base = gptp.sd->rxiov.iov_base;
	vec.iov_len = gptp.sd->rxiov.iov_len;

	iov_iter_init(&gptp.sd->rx_msg_hdr.msg_iter, READ | ITER_KVEC, &gptp.sd->rxiov, 1, GPTP_RX_BUF_SIZE);

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = kernel_recvmsg(gptp.sd->sock, &gptp.sd->rx_msg_hdr, &vec, 1, gptp.sd->rxiov.iov_len, 0);
	set_fs(oldfs);

	if (err > 0) {
		evt = gptp_parse_msg();
		gptp_handle_event(evt);
	}

	mod_timer(&gptp_callback_timer, jiffies + msecs_to_jiffies(TIMEOUT));
}

int gptp_sock_init(void)
{
	int err = 0;
	struct net_device *dev = NULL;
	struct net *net;
	int ts_opts = 0;
	struct timeval rx_timeout;
	struct cpsw_common *cpsw;
	struct timespec64 ts;
	rx_timeout.tv_sec = 1;
	rx_timeout.tv_usec = 0;

	/* Initialize modules */
	init_dm(&gptp);
	// initBMC(&gPTPd);
	init_cs(&gptp);

	/* Init the state machines */
	dm_handle_event(&gptp, GPTP_EVT_STATE_ENTRY);
	// bmcHandleEvent(&gPTPd, GPTP_EVT_STATE_ENTRY);

	if ((gptp.sd = (struct socketdata *) kmalloc(sizeof(struct socketdata),
						     GFP_ATOMIC)) == NULL) {
		printk(KERN_DEBUG "Unable to allocate required\
		       kernel memory for socketdata\n");
		return ENOMEM;
	}

	if ((err = sock_create(AF_PACKET, SOCK_RAW, htons(ETH_P_1588),
			      &gptp.sd->sock)) != 0) {
		printk(KERN_DEBUG "Unable to create raw socket\n");
		return err;
	}

	net = sock_net(gptp.sd->sock->sk);
	dev = dev_get_by_name_rcu(net, "eth0");

	memcpy(&gptp.sd->srcmac[0], dev->dev_addr, 6);
	gptp.sd->ifidx = dev->ifindex;

	/* Set timestamp options */
	ts_opts = SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_TX_SOFTWARE 
		 | SOF_TIMESTAMPING_SOFTWARE | SOF_TIMESTAMPING_OPT_CMSG | \
		 SOF_TIMESTAMPING_OPT_ID;
	
	if ((err = kernel_setsockopt(gptp.sd->sock, SOL_SOCKET, 
				     SO_TIMESTAMPING_OLD, (void *) &ts_opts,
				     sizeof(ts_opts))) != 0) {
		printk(KERN_ERR "Error in setting timestamping options\n");
		return err;
	}
	
	if ((err = kernel_setsockopt(gptp.sd->sock, SOL_SOCKET,
				     SO_RCVTIMEO_OLD, (void *) &rx_timeout,
				     sizeof(rx_timeout))) != 0) {
		printk(KERN_ERR "Error in setting socket rx timeout options\n");
		return err;
	}

	ts_opts = 1;
	
	if ((err = kernel_setsockopt(gptp.sd->sock, SOL_SOCKET, 
				     SO_SELECT_ERR_QUEUE,(void *) &ts_opts,
				     sizeof(ts_opts))) != 0) {
		printk(KERN_ERR "Error in setting err queue\
		       optins for socket\n");
		return err;
	}

	ts_opts = 1;

	if ((err = kernel_setsockopt(gptp.sd->sock, SOL_SOCKET, SO_REUSEADDR,
				    (void *) &ts_opts, 
				    sizeof(ts_opts))) != 0) {
		printk(KERN_ERR "Error in setting reuse optins for socket\n");
		return err;
	}

	if ((err = kernel_setsockopt(gptp.sd->sock, SOL_SOCKET, 
				     SO_BINDTODEVICE, (void *) dev->name,
				     IFNAMSIZ - 1)) != 0) {  
		printk(KERN_ERR "Error in binding socket to device\n");  
		return err;                                                     
	}

	rtnl_lock();
	dev_set_promiscuity(dev, 1);
	rtnl_unlock();
	
	/* Index of the network device */
	gptp.sd->tx_sock_address.sll_family = AF_PACKET;
	gptp.sd->tx_sock_address.sll_protocol = htons(ETH_P_1588);
	gptp.sd->tx_sock_address.sll_ifindex = gptp.sd->ifidx;
	/* Address length*/
	gptp.sd->tx_sock_address.sll_halen = ETH_ALEN;
	/* Destination MAC */
	gptp.sd->tx_sock_address.sll_addr[0] = 0x01;
	gptp.sd->tx_sock_address.sll_addr[1] = 0x80;
	gptp.sd->tx_sock_address.sll_addr[2] = 0xC2;
	gptp.sd->tx_sock_address.sll_addr[3] = 0x00;
	gptp.sd->tx_sock_address.sll_addr[4] = 0x00;
	gptp.sd->tx_sock_address.sll_addr[5] = 0x0E;

	/* Set the message header */
	gptp.sd->tx_msg_hdr.msg_control = NULL;
	gptp.sd->tx_msg_hdr.msg_controllen = 0;
	gptp.sd->tx_msg_hdr.msg_flags = 0;
	gptp.sd->tx_msg_hdr.msg_name = &gptp.sd->tx_sock_address;
	gptp.sd->tx_msg_hdr.msg_namelen = sizeof(struct sockaddr_ll);
	gptp.sd->tx_msg_hdr.msg_iocb = NULL;

	/* Index of the network device */
	gptp.sd->rx_sock_address.sll_family = AF_PACKET;
	gptp.sd->rx_sock_address.sll_protocol = htons(ETH_P_1588);
	gptp.sd->rx_sock_address.sll_ifindex = gptp.sd->ifidx;
	/* Address length*/
	gptp.sd->rx_sock_address.sll_halen = ETH_ALEN;
	/* Destination MAC */
	gptp.sd->rx_sock_address.sll_addr[0] = 0x01;
	gptp.sd->rx_sock_address.sll_addr[1] = 0x80;
	gptp.sd->rx_sock_address.sll_addr[2] = 0xC2;
	gptp.sd->rx_sock_address.sll_addr[3] = 0x00;
	gptp.sd->rx_sock_address.sll_addr[4] = 0x00;
	gptp.sd->rx_sock_address.sll_addr[5] = 0x0E;

	/* Set the message header */
	gptp.sd->rxiov.iov_base = gptp.sd->rx_buf;
	gptp.sd->rxiov.iov_len  = GPTP_RX_BUF_SIZE;
	gptp.sd->rx_msg_hdr.msg_iocb = NULL;
	gptp.sd->rx_msg_hdr.msg_control = gptp.sd->ts_buf;
	gptp.sd->rx_msg_hdr.msg_controllen = GPTP_CON_TS_BUF_SIZE;
	gptp.sd->rx_msg_hdr.msg_flags = 0;
	gptp.sd->rx_msg_hdr.msg_name = &gptp.sd->rx_sock_address;
	gptp.sd->rx_msg_hdr.msg_namelen = sizeof(struct sockaddr_ll);	
	
	// Retrieve ptp hw clock
	cpsw = ndev_to_cpsw(dev);
	gptp.ptp_clock = &cpsw->cpts->info;

	printk(KERN_DEBUG "PTP HW Clock name: %s", gptp.ptp_clock->name);
	gptp.ptp_clock->gettimex64(gptp.ptp_clock, &ts, NULL);
	printk(KERN_DEBUG "PTP HW time seconds: %lld, nanosecond %ld", 
	       ts.tv_sec, ts.tv_nsec);

	// iov_iter_init(&gptp.sd->rx_msg_hdr.msg_iter, READ | ITER_KVEC, &gptp.sd->rxiov, 1, 
	// 	     GPTP_RX_BUF_SIZE);

	gptp.sd->is_init = true;

	return 0;	
}

void gptp_worker_fn(struct work_struct *work)
{
	printk(KERN_INFO "Hello from the workqueue\n");
	printk(KERN_DEBUG "Init socket\n");
	
	if (gptp_sock_init() != 0) {
		printk(KERN_ERR "Unable to create socket\n");
		return;
	}

	timer_setup(&gptp_callback_timer, gptp_timer_callback, 0);
	mod_timer(&gptp_callback_timer, jiffies + msecs_to_jiffies(TIMEOUT));
}


static int __init gptp_init(void)
{
	printk(KERN_INFO "Initialising gptp worker\n");

	if ((gptp_workqueue = alloc_workqueue("gptp_worker", WQ_UNBOUND | WQ_MEM_RECLAIM, 1)) == NULL) {
		printk(KERN_ERR "Not able to allocate gptp_worker\n");
		goto out_of_mem;
	}

	printk(KERN_DEBUG "Starting gptp work queue\n");
	queue_work(gptp_workqueue, &gptp_work);

	return 0;
out_of_mem:
	return ENOMEM;
}

static void __exit gptp_exit(void)
{
	printk(KERN_DEBUG "Uninitializing state machines\n");
	deinit_cs(&gptp);
	deinit_dm(&gptp);
	printk(KERN_DEBUG "Freeing acquired socketdata memory\n");
	kfree(gptp.sd);
	printk(KERN_DEBUG "Stopping timer\n");
	del_timer(&gptp_callback_timer);
	printk(KERN_DEBUG "Destroying gptp worker queue\n");
	cancel_work_sync(&gptp_work);
	flush_workqueue(gptp_workqueue);
	destroy_workqueue(gptp_workqueue);

	printk(KERN_INFO "Shutdown of gptp worker finished\n");
}

module_init(gptp_init);
module_exit(gptp_exit);

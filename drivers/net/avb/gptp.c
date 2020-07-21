#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <net/sock.h>
#include <linux/netdevice.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include "gptp_common.h"

#define TIMEOUT 100 //ms
#define GPTP_TX_BUF_SIZE                  1024
#define GPTP_RX_BUF_SIZE                  4096
#define GPTP_CON_TS_BUF_SIZE              1024

MODULE_DESCRIPTION("Kernel Dummy Module");
MODULE_AUTHOR("Niklas Wantrupp");
MODULE_LICENSE("GPL");

void gptp_timer_callback(struct timer_list *data);
void gptp_worker_fn(struct work_struct *work);
int gptp_sock_init(void);

static int count = 0;
static struct timer_list gptp_callback_timer;
static struct workqueue_struct *gptp_workqueue;
static DECLARE_WORK(gptp_work, gptp_worker_fn);
static struct gptp_instance gptp;

void gptp_timer_callback(struct timer_list *data)
{
	count++;
	printk(KERN_INFO "Timer callback fired. Count: %d\n", count);

	mod_timer(&gptp_callback_timer, jiffies + msecs_to_jiffies(TIMEOUT));
}

int gptp_sock_init(void)
{
	int err = 0;
	struct net_device *dev = NULL;
	struct net *net;
	int ts_opts = 0;
	struct timeval rx_timeout;
	rx_timeout.tv_sec = 1;
	rx_timeout.tv_usec = 0;

	if ((gptp.sd = (struct socketdata *) kmalloc(sizeof(struct socketdata), GFP_ATOMIC)) == NULL) {
		printk(KERN_DEBUG "Unable to allocate required kernel memory for socketdata\n");
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
	
	if ((err = kernel_setsockopt(gptp.sd->sock, SOL_SOCKET, SO_TIMESTAMPING_OLD, 
				    (void *) &ts_opts, sizeof(ts_opts))) != 0) {
		printk(KERN_ERR "Error in setting timestamping options\n");
		return err;
	}
	
	if ((err = kernel_setsockopt(gptp.sd->sock, SOL_SOCKET, SO_RCVTIMEO_OLD, 
				    (void *) &rx_timeout, sizeof(rx_timeout))) != 0) {
		printk(KERN_ERR "Error in setting socket rx timeout options\n");
		return err;
	}

	ts_opts = 1;
	
	if ((err = kernel_setsockopt(gptp.sd->sock, SOL_SOCKET, SO_SELECT_ERR_QUEUE,
				    (void *) &ts_opts, sizeof(ts_opts))) != 0) {
		printk(KERN_ERR "Error in setting err queue optins for socket\n");
		return err;
	}

	ts_opts = 1;

	if ((err = kernel_setsockopt(gptp.sd->sock, SOL_SOCKET, SO_REUSEADDR,
				    (void *) &ts_opts, sizeof(ts_opts))) != 0) {
		printk(KERN_ERR "Error in setting reuse optins for socket\n");
		return err;
	}

	if ((err = kernel_setsockopt(gptp.sd->sock, SOL_SOCKET, SO_BINDTODEVICE,
				    (void *) dev->name, IFNAMSIZ - 1)) != 0) {  
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

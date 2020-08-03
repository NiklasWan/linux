#include "avb_common.h"

u32 avb_change_to_big_endian(u32 val)
{
	u32 test  = 0x12345678;
	u8* msb   = (u8*)&test;
	u32 bval  = val;
	u8* bytes = (u8*)&bval;
	u8 tmp    = 0;

	if(*msb != 0x12) {
		tmp = bytes[0];
		bytes[0] = bytes[3];
		bytes[3] = tmp; 
		tmp = bytes[1];
		bytes[1] = bytes[2];
		bytes[2] = tmp; 
	}

	return bval;
}

u16 avb_change_to_big_endian_u16(u16 val)
{
	u16 test  = 0x1234;
	u8* msb   = (u8*)&test;
	u16 bval  = val;
	u8* bytes = (u8*)&bval;
	u8 tmp    = 0;

	if(*msb != 0x12) {
		tmp = bytes[0];
		bytes[0] = bytes[1];
		bytes[1] = tmp;
	}

	return bval;
}

bool avb_socket_init(struct socketdata* sd, int rx_timeout)
{
	int err = 0;
	struct net_device *dev = NULL;
	struct net *net;
	struct timeval ts_opts;
	ktime_t hw_time;     
	ts_opts.tv_sec = (rx_timeout / 1000);
	ts_opts.tv_usec = (rx_timeout % 1000);

	printk(KERN_INFO "avb_socket_init");

	if ((err = sock_create(AF_PACKET, SOCK_RAW, htons(sd->type), &sd->sock)) != 0) {
		printk(KERN_ERR "avb_socket_init Socket creation fails %d \n", err);
		return false;
	}

	net = sock_net(sd->sock->sk);
	dev = dev_get_by_name_rcu(net, "eth0");

	memcpy(&sd->srcmac[0], dev->dev_addr, 6);
	sd->ifidx = dev->ifindex;

	rtnl_lock();
	dev_set_promiscuity(dev, 1);
	rtnl_unlock();

	if ((err = kernel_setsockopt(sd->sock, SOL_SOCKET, SO_RCVTIMEO_OLD, (void *) &ts_opts, sizeof(ts_opts))) != 0) {
		printk(KERN_WARNING "avb_msrp_init set rx timeout fails %d\n", err);
		return false;
	}

	if ((err = __avb_ptp_clock.ptp_clock.register_clock(dev)) != 0) {
		printk(KERN_ERR "Initialization of PTP HW clock failed\n");
		return false;
	} else {
		hw_time = get_avb_ptp_time();
		printk(KERN_DEBUG "Initialization of PTP HW clock time: %lld nanoseconds\n", hw_time);
	}

	/* Index of the network device */
	sd->tx_sock_address.sll_family = AF_PACKET;
	sd->tx_sock_address.sll_protocol = htons(sd->type);
	sd->tx_sock_address.sll_ifindex = sd->ifidx;
	/* Address length*/
	sd->tx_sock_address.sll_halen = ETH_ALEN;
	/* Destination MAC */
	sd->tx_sock_address.sll_addr[0] = sd->destmac[0];
	sd->tx_sock_address.sll_addr[1] = sd->destmac[1];
	sd->tx_sock_address.sll_addr[2] = sd->destmac[2];
	sd->tx_sock_address.sll_addr[3] = sd->destmac[3];
	sd->tx_sock_address.sll_addr[4] = sd->destmac[4];
	sd->tx_sock_address.sll_addr[5] = sd->destmac[5];

	/* Set the message header */
	sd->tx_msg_hdr.msg_control=NULL;
	sd->tx_msg_hdr.msg_controllen=0;
	sd->tx_msg_hdr.msg_flags=0;
	sd->tx_msg_hdr.msg_name=&sd->tx_sock_address;
	sd->tx_msg_hdr.msg_namelen=sizeof(struct sockaddr_ll);
	sd->tx_msg_hdr.msg_iocb = NULL;

	/* Index of the network device */
	sd->rx_sock_address.sll_family = AF_PACKET;
	sd->rx_sock_address.sll_protocol = htons(sd->type);
	sd->rx_sock_address.sll_ifindex = sd->ifidx;
	/* Address length*/
	sd->rx_sock_address.sll_halen = ETH_ALEN;
	/* Destination MAC */
	sd->rx_sock_address.sll_addr[0] = sd->destmac[0];
	sd->rx_sock_address.sll_addr[1] = sd->destmac[1];
	sd->rx_sock_address.sll_addr[2] = sd->destmac[2];
	sd->rx_sock_address.sll_addr[3] = sd->destmac[3];
	sd->rx_sock_address.sll_addr[4] = sd->destmac[4];
	sd->rx_sock_address.sll_addr[5] = sd->destmac[5];

	/* Set the message header */
	sd->rx_msg_hdr.msg_control=NULL;
	sd->rx_msg_hdr.msg_controllen=0;
	sd->rx_msg_hdr.msg_flags=0;
	sd->rx_msg_hdr.msg_name=&sd->rx_sock_address;
	sd->rx_msg_hdr.msg_namelen=sizeof(struct sockaddr_ll);
	sd->rx_msg_hdr.msg_iocb = NULL;
	sd->rxiov.iov_base = sd->rx_buf;
	sd->rxiov.iov_len = AVB_MAX_ETH_FRAME_SIZE;
	iov_iter_init(&sd->rx_msg_hdr.msg_iter, READ, &sd->rxiov, 1, AVB_MAX_ETH_FRAME_SIZE);

	return true;
}

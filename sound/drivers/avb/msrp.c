#include "msrp.h"

bool avb_msrp_init(struct msrp* msrp)
{
	printk(KERN_INFO "avb_msrp_init");

	msrp->rx_state = MSRP_DECLARATION_STATE_NONE;
	msrp->tx_state = MSRP_DECLARATION_STATE_NONE;

	msrp->sd.type = ETH_MSRP;
	msrp->sd.destmac[0] = 0x01;
	msrp->sd.destmac[1] = 0x80;
	msrp->sd.destmac[2] = 0xC2;
	msrp->sd.destmac[3] = 0x00;
	msrp->sd.destmac[4] = 0x00;
	msrp->sd.destmac[5] = 0x0E;

	return avb_socket_init(&msrp->sd, 1000);
}

void avb_msrp_talkerdeclarations(struct msrp* msrp, bool join, int state)
{
	int tx_size = 0;
	int err = 0;
	struct ethhdr *eh = (struct ethhdr *)&msrp->sd.tx_buf[0];
	struct talker_msr_pdu *pdu = (struct talker_msr_pdu*)&msrp->sd.tx_buf[sizeof(struct ethhdr)];
	struct kvec vec;

	printk(KERN_INFO "avb_msrp_talkerdeclarations join: %d, state: %d", join, state);

	/* Initialize it */
	memset(msrp->sd.tx_buf, 0, AVB_MAX_ETH_FRAME_SIZE);

	/* Fill in the Ethernet header */
	eh->h_dest[0] = 0x01;
	eh->h_dest[1] = 0x80;
	eh->h_dest[2] = 0xC2;
	eh->h_dest[3] = 0x00;
	eh->h_dest[4] = 0x00;
	eh->h_dest[5] = 0x0E;
	eh->h_source[0] = msrp->sd.srcmac[0];
	eh->h_source[1] = msrp->sd.srcmac[1];
	eh->h_source[2] = msrp->sd.srcmac[2];
	eh->h_source[3] = msrp->sd.srcmac[3];
	eh->h_source[4] = msrp->sd.srcmac[4];
	eh->h_source[5] = msrp->sd.srcmac[5];

	/* Fill in Ethertype field */
	eh->h_proto = htons(msrp->sd.type);

	pdu->protocol_version = 0;
	pdu->msg.attribute_type = MSRP_ATTRIBUTE_TYPE_TALKER_ADVERTISE_VECTOR;
	pdu->msg.attribute_len  = MSRP_ATTRIBUTE_LEN_TALKER_ADVERTISE_VECTOR;
	pdu->msg.attribute_list_len = avb_change_to_big_endian_u16(sizeof(struct talker_vector_attribute));

	if(state != MSRP_DECLARATION_STATE_UNKNOWN) {
		pdu->msg.attibute_list.hdr.number_of_values = avb_change_to_big_endian_u16(1);
		pdu->msg.attibute_list.val.stream_id[0] = msrp->sd.srcmac[0];
		pdu->msg.attibute_list.val.stream_id[1] = msrp->sd.srcmac[1];
		pdu->msg.attibute_list.val.stream_id[2] = msrp->sd.srcmac[2];
		pdu->msg.attibute_list.val.stream_id[3] = msrp->sd.srcmac[3];
		pdu->msg.attibute_list.val.stream_id[4] = msrp->sd.srcmac[4];
		pdu->msg.attibute_list.val.stream_id[5] = msrp->sd.srcmac[5];
		pdu->msg.attibute_list.val.stream_id[6] = 0;
		pdu->msg.attibute_list.val.stream_id[7] = 1;

		pdu->msg.attibute_list.val.data_frame_params[0] = msrp->sd.destmac[0];
		pdu->msg.attibute_list.val.data_frame_params[1] = msrp->sd.destmac[1];
		pdu->msg.attibute_list.val.data_frame_params[2] = msrp->sd.destmac[2];
		pdu->msg.attibute_list.val.data_frame_params[3] = msrp->sd.destmac[3];
		pdu->msg.attibute_list.val.data_frame_params[4] = msrp->sd.destmac[4];
		pdu->msg.attibute_list.val.data_frame_params[5] = msrp->sd.destmac[5];
		pdu->msg.attibute_list.val.data_frame_params[6] = 0;
		pdu->msg.attibute_list.val.data_frame_params[7] = 2;

		pdu->msg.attibute_list.val.max_frame_size = avb_change_to_big_endian_u16(MSRP_MAX_FRAME_SIZE_48KHZ_AUDIO);
		pdu->msg.attibute_list.val.max_interval_frames = avb_change_to_big_endian_u16(MSRP_MAX_INTERVAL_FRAME_48KHZ_AUDIO);
		pdu->msg.attibute_list.val.priority_and_rank = 0;
		pdu->msg.attibute_list.val.accumalated_latency = avb_change_to_big_endian(0);

		pdu->msg.attibute_list.vector[0] = MSRP_THREE_PACK(((join == true)?(MSRP_ATTRIBUTE_EVENT_JOININ):(MSRP_ATTRIBUTE_EVENT_LEAVE)), 0, 0);
	} else {
		pdu->msg.attibute_list.vector[0] = MSRP_THREE_PACK(MSRP_ATTRIBUTE_EVENT_JOINMT, 0, 0);
	}

	pdu->msg.end_marker = 0;
	pdu->end_marker = 0;

	tx_size = sizeof(struct ethhdr) + sizeof(struct talker_msr_pdu);

	msrp->sd.txiov.iov_base = msrp->sd.tx_buf;
	msrp->sd.txiov.iov_len = tx_size;

	vec.iov_base = msrp->sd.txiov.iov_base;
	vec.iov_len = msrp->sd.txiov.iov_len;

	iov_iter_init(&msrp->sd.tx_msg_hdr.msg_iter, WRITE, &msrp->sd.txiov, 1, tx_size);

	if ((err = kernel_sendmsg(msrp->sd.sock, &msrp->sd.tx_msg_hdr, &vec, 1, tx_size)) <= 0) {
		printk(KERN_WARNING "avb_msrp_talkerdeclarations Socket transmission fails %d \n", err);
		return;
	}
}

void avb_msrp_listenerdeclarations(struct msrp* msrp, bool join, int state)
{
	int tx_size = 0;
	int err = 0;
	struct ethhdr *eh = (struct ethhdr *)&msrp->sd.tx_buf[0];
	struct listner_msr_pdu *pdu = (struct listner_msr_pdu*)&msrp->sd.tx_buf[sizeof(struct ethhdr)];
	struct kvec vec;

	printk(KERN_INFO "avb_msrp_listenerdeclarations join: %d, state: %d", join, state);

	/* Initialize it */
	memset(msrp->sd.tx_buf, 0, AVB_MAX_ETH_FRAME_SIZE);

	/* Fill in the Ethernet header */
	eh->h_dest[0] = 0x01;
	eh->h_dest[1] = 0x80;
	eh->h_dest[2] = 0xC2;
	eh->h_dest[3] = 0x00;
	eh->h_dest[4] = 0x00;
	eh->h_dest[5] = 0x0E;
	eh->h_source[0] = msrp->sd.srcmac[0];
	eh->h_source[1] = msrp->sd.srcmac[1];
	eh->h_source[2] = msrp->sd.srcmac[2];
	eh->h_source[3] = msrp->sd.srcmac[3];
	eh->h_source[4] = msrp->sd.srcmac[4];
	eh->h_source[5] = msrp->sd.srcmac[5];

	/* Fill in Ethertype field */
	eh->h_proto = htons(msrp->sd.type);

	pdu->protocol_version = 0;
	pdu->msg.attribute_type = MSRP_ATTRIBUTE_TYPE_LISTENER_VECTOR;
	pdu->msg.attribute_len  = MSRP_ATTRIBUTE_LEN_LISTENER_VECTOR;
	pdu->msg.attribute_list_len = avb_change_to_big_endian_u16(sizeof(struct listner_vector_attribute));

	if(state != MSRP_DECLARATION_STATE_UNKNOWN) {
		pdu->msg.attibute_list.hdr.number_of_values = avb_change_to_big_endian_u16(1);
		pdu->msg.attibute_list.val.stream_id[0] = msrp->stream_id[0];
		pdu->msg.attibute_list.val.stream_id[1] = msrp->stream_id[1];
		pdu->msg.attibute_list.val.stream_id[2] = msrp->stream_id[2];
		pdu->msg.attibute_list.val.stream_id[3] = msrp->stream_id[3];
		pdu->msg.attibute_list.val.stream_id[4] = msrp->stream_id[4];
		pdu->msg.attibute_list.val.stream_id[5] = msrp->stream_id[5];
		pdu->msg.attibute_list.val.stream_id[6] = msrp->stream_id[6];
		pdu->msg.attibute_list.val.stream_id[7] = msrp->stream_id[7];

		pdu->msg.attibute_list.vector[0] = MSRP_THREE_PACK(((join == true)?(MSRP_ATTRIBUTE_EVENT_JOININ):(MSRP_ATTRIBUTE_EVENT_LEAVE)), 0, 0);
		pdu->msg.attibute_list.vector[1] = MSRP_FOUR_PACK(state, 0, 0, 0);
	} else {
		pdu->msg.attibute_list.vector[0] = MSRP_THREE_PACK(MSRP_ATTRIBUTE_EVENT_JOINMT, 0, 0);
		pdu->msg.attibute_list.vector[1] = MSRP_FOUR_PACK(MSRP_DECLARATION_STATE_READY, 0, 0, 0);
	}

	pdu->msg.end_marker = 0;
	pdu->end_marker = 0;

	tx_size = sizeof(struct ethhdr) + sizeof(struct listner_msr_pdu);

	msrp->sd.txiov.iov_base = msrp->sd.tx_buf;
	msrp->sd.txiov.iov_len = tx_size;

	vec.iov_base = msrp->sd.txiov.iov_base;
	vec.iov_len = msrp->sd.txiov.iov_len;

	iov_iter_init(&msrp->sd.tx_msg_hdr.msg_iter, WRITE, &msrp->sd.txiov, 1, tx_size);

	if ((err = kernel_sendmsg(msrp->sd.sock, &msrp->sd.tx_msg_hdr, &vec, 1, tx_size)) <= 0) {
		printk(KERN_WARNING "avb_msrp_listenerdeclarations Socket transmission fails %d \n", err);
		return;
	}
}

void avb_msrp_domaindeclarations(struct msrp* msrp)
{
	int tx_size = 0;
	int err = 0;
	struct ethhdr *eh = (struct ethhdr *)&msrp->sd.tx_buf[0];
	struct domain_msr_pdu *pdu = (struct domain_msr_pdu*)&msrp->sd.tx_buf[sizeof(struct ethhdr)];
	struct kvec vec;

	printk(KERN_INFO "avb_msrp_domaindeclarations");

	/* Initialize it */
	memset(msrp->sd.tx_buf, 0, AVB_MAX_ETH_FRAME_SIZE);

	/* Fill in the Ethernet header */
	eh->h_dest[0] = 0x01;
	eh->h_dest[1] = 0x80;
	eh->h_dest[2] = 0xC2;
	eh->h_dest[3] = 0x00;
	eh->h_dest[4] = 0x00;
	eh->h_dest[5] = 0x0E;
	eh->h_source[0] = msrp->sd.srcmac[0];
	eh->h_source[1] = msrp->sd.srcmac[1];
	eh->h_source[2] = msrp->sd.srcmac[2];
	eh->h_source[3] = msrp->sd.srcmac[3];
	eh->h_source[4] = msrp->sd.srcmac[4];
	eh->h_source[5] = msrp->sd.srcmac[5];

	/* Fill in Ethertype field */
	eh->h_proto = htons(msrp->sd.type);

	pdu->protocol_version = 0;
	pdu->msg.attribute_type = MSRP_ATTRIBUTE_TYPE_DOMAIN_VECTOR;
	pdu->msg.attribute_len  = MSRP_ATTRIBUTE_LEN_DOMAIN_VECTOR;
	pdu->msg.attribute_list_len = avb_change_to_big_endian_u16(sizeof(struct domain_vector_attribute) + 2);
	pdu->msg.attibute_list.hdr.number_of_values = avb_change_to_big_endian_u16(1);
	pdu->msg.attibute_list.val.sr_class_id = 2;
	pdu->msg.attibute_list.val.sr_class_prio = 3;
	pdu->msg.attibute_list.val.sr_class_VID = avb_change_to_big_endian_u16(2);
	pdu->msg.attibute_list.vector[0] = MSRP_THREE_PACK(MSRP_ATTRIBUTE_EVENT_JOININ, 0, 0);
	pdu->msg.end_marker = 0;
	pdu->end_marker = 0;

	tx_size = sizeof(struct ethhdr) + sizeof(struct domain_msr_pdu);

	msrp->sd.txiov.iov_base = msrp->sd.tx_buf;
	msrp->sd.txiov.iov_len = tx_size;

	vec.iov_base = msrp->sd.txiov.iov_base;
	vec.iov_len = msrp->sd.txiov.iov_len;

	iov_iter_init(&msrp->sd.tx_msg_hdr.msg_iter, WRITE, &msrp->sd.txiov, 1, tx_size);

	if ((err = kernel_sendmsg(msrp->sd.sock, &msrp->sd.tx_msg_hdr, &vec, 1, tx_size)) <= 0) {
		printk(KERN_WARNING "avb_msrp_domaindeclarations Socket transmission fails %d \n", err);
		return;
	}
}

static void avb_msrp_evaluate_talker_advertisement(struct msrp* msrp)
{
	struct talker_msr_pdu *tpdu = (struct talker_msr_pdu*)&msrp->sd.rx_buf[sizeof(struct ethhdr)];

	int leave_all = avb_change_to_big_endian_u16(tpdu->msg.attibute_list.hdr.number_of_values) & 0x2000;
	int evt = MSRP_THREE_PACK_GET_A(tpdu->msg.attibute_list.vector[0]);

	if(leave_all != 0) {
		msrp->tx_state = MSRP_DECLARATION_STATE_UNKNOWN;
	} else {
		if(tpdu->msg.attribute_type == MSRP_ATTRIBUTE_TYPE_TALKER_ADVERTISE_VECTOR) {
			if((evt == MSRP_ATTRIBUTE_EVENT_JOINMT) || (evt == MSRP_ATTRIBUTE_EVENT_MT) || (evt == MSRP_ATTRIBUTE_EVENT_LEAVE)) {
				msrp->rx_state = MSRP_DECLARATION_STATE_NONE;
				memset(&msrp->stream_id[0], 0, 8);
			} else {
				msrp->rx_state = MSRP_DECLARATION_STATE_READY;
			}
		} else {
			msrp->rx_state = MSRP_DECLARATION_STATE_ASKING_FAILED;	
		}

		memcpy(&msrp->stream_id[0], &tpdu->msg.attibute_list.val.stream_id[0], 8);
	}
}

static void avb_msrp_evaluate_listner_advertisement(struct msrp* msrp)
{
	struct listner_msr_pdu *pdu = (struct listner_msr_pdu*)&msrp->sd.rx_buf[sizeof(struct ethhdr)];

	int leave_all = avb_change_to_big_endian_u16(pdu->msg.attibute_list.hdr.number_of_values) & 0x2000;
	int evt = MSRP_THREE_PACK_GET_A(pdu->msg.attibute_list.vector[0]);

	if(leave_all != 0) {
		msrp->rx_state = MSRP_DECLARATION_STATE_UNKNOWN;
	} else {
		if((evt == MSRP_ATTRIBUTE_EVENT_JOINMT) || (evt == MSRP_ATTRIBUTE_EVENT_MT) || (evt == MSRP_ATTRIBUTE_EVENT_LEAVE)) {
			msrp->tx_state = MSRP_DECLARATION_STATE_NONE;
		} else {
			if((pdu->msg.attibute_list.val.stream_id[0] == msrp->sd.srcmac[0]) &&
			   (pdu->msg.attibute_list.val.stream_id[1] == msrp->sd.srcmac[1]) && 
			   (pdu->msg.attibute_list.val.stream_id[2] == msrp->sd.srcmac[2]) && 
			   (pdu->msg.attibute_list.val.stream_id[3] == msrp->sd.srcmac[3]) && 
			   (pdu->msg.attibute_list.val.stream_id[4] == msrp->sd.srcmac[4]) && 
			   (pdu->msg.attibute_list.val.stream_id[5] == msrp->sd.srcmac[5]) &&
			   (pdu->msg.attibute_list.val.stream_id[6] == 0) && 
			   (pdu->msg.attibute_list.val.stream_id[7] == 1)) {
				msrp->tx_state = MSRP_DECLARATION_STATE_READY;
			}
		}
	}
}

int avb_msrp_listen(struct msrp* msrp)
{
	int err = 0;
	mm_segment_t oldfs;
	struct listner_msr_pdu *tpdu = (struct listner_msr_pdu*)&msrp->sd.rx_buf[sizeof(struct ethhdr)];
	struct kvec vec;

	memset(msrp->sd.rx_buf, 0, AVB_MAX_ETH_FRAME_SIZE);
	msrp->sd.rxiov.iov_base = msrp->sd.rx_buf;
	msrp->sd.rxiov.iov_len = AVB_MAX_ETH_FRAME_SIZE;

	vec.iov_base = msrp->sd.rxiov.iov_base;
	vec.iov_len = msrp->sd.rxiov.iov_len;

	iov_iter_init(&msrp->sd.rx_msg_hdr.msg_iter, READ, &msrp->sd.rxiov, 1, AVB_MAX_ETH_FRAME_SIZE);

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = kernel_recvmsg(msrp->sd.sock, &msrp->sd.rx_msg_hdr, &vec, 1 , msrp->sd.rxiov.iov_len, 0);
	set_fs(oldfs);
	
	if (err <= 0) {
		if(err != -11)
			printk(KERN_WARNING "avb_msrp_listen Socket reception res %d \n", err);
	} else {
		if(tpdu->protocol_version != 0) {
			printk(KERN_WARNING "avb_msrp_listen unknown protocol_version %d \n", tpdu->protocol_version);
		} else {
			if((tpdu->msg.attribute_type == MSRP_ATTRIBUTE_TYPE_TALKER_ADVERTISE_VECTOR) ||
			   (tpdu->msg.attribute_type == MSRP_ATTRIBUTE_TYPE_TALKER_FAILED_VECTOR)) {
				avb_msrp_evaluate_talker_advertisement(msrp);
			} else if(tpdu->msg.attribute_type == MSRP_ATTRIBUTE_TYPE_LISTENER_VECTOR) {
				avb_msrp_evaluate_listner_advertisement(msrp);
			} else if(tpdu->msg.attribute_type == MSRP_ATTRIBUTE_TYPE_DOMAIN_VECTOR) {
			} else {
				printk(KERN_WARNING "avb_msrp_listen unknown attribute type %d \n", tpdu->msg.attribute_type);
			}

			printk(KERN_NOTICE "avb_msrp_listen: rxType: %d, rx_state: %d, tx_state: %d", tpdu->msg.attribute_type,
				msrp->rx_state, msrp->tx_state);		
		}
	}

	return err;
}
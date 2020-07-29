/*
 *  Virtual AVB ALSA sound device
 *
 *  Implementation:
 *  Copyright (c) by Niklas Wantrupp <niklaswantrupp@web.de>
 *
 *  Based on generic AVB Driver:
 *  Copyright (c) Indumathi Duraipandian <indu9086@gmail.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/info.h>
#include <sound/initval.h>
#include <sound/hwdep.h>

#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/delay.h>
#include <linux/un.h>
#include <linux/unistd.h>
#include <linux/ctype.h>
#include <linux/hrtimer.h>

#include <asm/unistd.h>
#include <asm/div64.h>

#include <net/sock.h>
#include <net/tcp.h>
#include <net/inet_connection_sock.h>
#include <net/request_sock.h>

#include "avb_types.h"

MODULE_AUTHOR("Indumathi Duraipandian <indu9086@gmail.com>");
MODULE_DESCRIPTION("AVB soundcard");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{ALSA,AVB soundcard}}");

#define AVB_DEBUG

static int index[SND_AVB_NUM_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SND_AVB_NUM_CARDS] = SNDRV_DEFAULT_STR;
static bool enable[SND_AVB_NUM_CARDS] = {1, [1 ... (SND_AVB_NUM_CARDS - 1)] = 0};
static int pcm_substreams[SND_AVB_NUM_CARDS] = {1};
static int pcm_notify[SND_AVB_NUM_CARDS];

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for avb soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for avb soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable this avb soundcard.");
module_param_array(pcm_substreams, int, NULL, 0444);
MODULE_PARM_DESC(pcm_substreams, "PCM substreams # (1-8) for avb driver.");
module_param_array(pcm_notify, int, NULL, 0444);
MODULE_PARM_DESC(pcm_notify, "Break capture when PCM format/rate/channels changes.");

static struct avbdevice avbdevice;
static int numcards = 0;
static struct platform_device *avbdevices[SND_AVB_NUM_CARDS];

static SIMPLE_DEV_PM_OPS(avb_pm, avb_suspend, avb_resume);
static struct platform_driver avb_driver = {
	.probe		= avb_probe,
	.remove		= avb_remove,
	.driver		= {
		.name	= SND_AVB_DRIVER,
		.pm	= AVB_PM_OPS,
	},
};

static struct snd_pcm_ops avb_playback_ops = {
	.open =		avb_playback_open,
	.close =	avb_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	avb_playback_hw_params,
	.hw_free =	avb_playback_hw_free,
	.prepare =	avb_playback_prepare,
	.trigger =	avb_playback_trigger,
	.pointer =	avb_playback_pointer,
	.copy_user = 	avb_playback_copy
};

static struct snd_pcm_ops avb_capture_ops = {
	.open =		avb_capture_open,
	.close =	avb_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	avb_capture_hw_params,
	.hw_free =	avb_capture_hw_free,
	.prepare =	avb_capture_prepare,
	.trigger =	avb_capture_trigger,
	.pointer =	avb_capture_pointer,
	.copy_user = 	avb_capture_copy
};

static struct snd_pcm_hardware avb_playback_hw = {
        .info = (SNDRV_PCM_INFO_INTERLEAVED |
                 SNDRV_PCM_INFO_BLOCK_TRANSFER),
        .formats =          SNDRV_PCM_FMTBIT_S16_LE,
        .rates =            SNDRV_PCM_RATE_8000_192000,
        .rate_min =         8000,
        .rate_max =         192000,
        .channels_min =     1,
        .channels_max =     8,
        .buffer_bytes_max = 131072,
        .period_bytes_min = 16384,
        .period_bytes_max = 131072,
        .periods_min =      1,
        .periods_max =      4,
};

static struct snd_pcm_hardware avb_capture_hw = {
        .info = (SNDRV_PCM_INFO_INTERLEAVED |
                 SNDRV_PCM_INFO_BLOCK_TRANSFER),
        .formats =          SNDRV_PCM_FMTBIT_S16_LE,
        .rates =            SNDRV_PCM_RATE_8000_192000,
        .rate_min =         8000,
        .rate_max =         192000,
        .channels_min =     1,
        .channels_max =     8,
        .buffer_bytes_max = 131072,
        .period_bytes_min = 16384,
        .period_bytes_max = 131072,
        .periods_min =      1,
        .periods_max =      4,
};

static u32 avb_change_to_big_endian(u32 val)
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

static u16 avb_change_to_big_endian_u16(u16 val)
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

static void avb_log(int level, char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	switch(level) {
		case AVB_KERN_EMERG:
			vprintk(fmt, args);
			break;
		case AVB_KERN_ALERT:
			vprintk(fmt, args);
			break;
		case AVB_KERN_CRIT:
			vprintk(fmt, args);
			break;
		case AVB_KERN_ERR:
			vprintk(fmt, args);
			break;
		case AVB_KERN_WARN:
			vprintk(fmt, args);
			break;
		case AVB_KERN_NOT:
			vprintk(fmt, args);
			break;
#ifdef AVB_DEBUG
		case AVB_KERN_INFO:
			vprintk(fmt, args);
			break;
		case AVB_KERN_DEBUG:
			vprintk(fmt, args);
			break;
#else
		default:
			break;
#endif
	}

	va_end(args);
}

static struct workdata* avb_init_and_queue_work(int workId, void* wdata, int delay)
{
	struct workdata* wd;

	wd = (struct workdata*)kmalloc(sizeof(struct workdata), GFP_KERNEL);

	if(wd == NULL) {
		avb_log(AVB_KERN_ERR, KERN_ERR "avb_init_and_queue_work workdata allocation failed for work %d", workId);
		return NULL;
	}

	wd->dw.data = wdata;
	wd->delayedWorkId = workId;
	INIT_DELAYED_WORK((struct delayed_work*)wd, avbWqFn);
			
	queue_delayed_work(avbdevice.wq, (struct delayed_work*)wd, delay);

	return wd;
}

static bool avb_socket_init(struct socketdata* sd, int rxTimeout)
{
	int err = 0;
	struct net_device *dev = NULL;
	struct net *net;
	struct timeval tsOpts;      
	tsOpts.tv_sec = (rxTimeout / 1000);
	tsOpts.tv_usec = (rxTimeout % 1000);

	avb_log(AVB_KERN_INFO, KERN_INFO "avb_socket_init");

	if ((err = sock_create(AF_PACKET, SOCK_RAW, htons(sd->type), &sd->sock)) != 0) {
		avb_log(AVB_KERN_ERR, KERN_ERR "avb_socket_init Socket creation fails %d \n", err);
		return false;
	}

	net = sock_net(sd->sock->sk);
	dev = dev_get_by_name_rcu(net, "eth0");

	memcpy(&sd->srcmac[0], dev->dev_addr, 6);
	sd->ifidx = dev->ifindex;

	rtnl_lock();
	dev_set_promiscuity(dev, 1);
	rtnl_unlock();

	if ((err = kernel_setsockopt(sd->sock, SOL_SOCKET, SO_RCVTIMEO_OLD, (void *) &tsOpts, sizeof(tsOpts))) != 0) {
		avb_log(AVB_KERN_WARN, KERN_WARNING "avb_msrp_init set rx timeout fails %d\n", err);
		return false;
	}

	/* Index of the network device */
	sd->txSockAddress.sll_family = AF_PACKET;
	sd->txSockAddress.sll_protocol = htons(sd->type);
	sd->txSockAddress.sll_ifindex = sd->ifidx;
	/* Address length*/
	sd->txSockAddress.sll_halen = ETH_ALEN;
	/* Destination MAC */
	sd->txSockAddress.sll_addr[0] = sd->destmac[0];
	sd->txSockAddress.sll_addr[1] = sd->destmac[1];
	sd->txSockAddress.sll_addr[2] = sd->destmac[2];
	sd->txSockAddress.sll_addr[3] = sd->destmac[3];
	sd->txSockAddress.sll_addr[4] = sd->destmac[4];
	sd->txSockAddress.sll_addr[5] = sd->destmac[5];

	/* Set the message header */
	sd->txMsgHdr.msg_control=NULL;
	sd->txMsgHdr.msg_controllen=0;
	sd->txMsgHdr.msg_flags=0;
	sd->txMsgHdr.msg_name=&sd->txSockAddress;
	sd->txMsgHdr.msg_namelen=sizeof(struct sockaddr_ll);
	sd->txMsgHdr.msg_iocb = NULL;

	/* Index of the network device */
	sd->rxSockAddress.sll_family = AF_PACKET;
	sd->rxSockAddress.sll_protocol = htons(sd->type);
	sd->rxSockAddress.sll_ifindex = sd->ifidx;
	/* Address length*/
	sd->rxSockAddress.sll_halen = ETH_ALEN;
	/* Destination MAC */
	sd->rxSockAddress.sll_addr[0] = sd->destmac[0];
	sd->rxSockAddress.sll_addr[1] = sd->destmac[1];
	sd->rxSockAddress.sll_addr[2] = sd->destmac[2];
	sd->rxSockAddress.sll_addr[3] = sd->destmac[3];
	sd->rxSockAddress.sll_addr[4] = sd->destmac[4];
	sd->rxSockAddress.sll_addr[5] = sd->destmac[5];

	/* Set the message header */
	sd->rxMsgHdr.msg_control=NULL;
	sd->rxMsgHdr.msg_controllen=0;
	sd->rxMsgHdr.msg_flags=0;
	sd->rxMsgHdr.msg_name=&sd->rxSockAddress;
	sd->rxMsgHdr.msg_namelen=sizeof(struct sockaddr_ll);
	sd->rxMsgHdr.msg_iocb = NULL;
	sd->rxiov.iov_base = sd->rxBuf;
	sd->rxiov.iov_len = AVB_MAX_ETH_FRAME_SIZE;
	iov_iter_init(&sd->rxMsgHdr.msg_iter, READ, &sd->rxiov, 1, AVB_MAX_ETH_FRAME_SIZE);

	return true;
}

static int avb_get_avtp_aaf_format(int rtformat)
{
	int format = AVB_AVTP_AAF_FORMAT_USER_SP;

	if((rtformat == SNDRV_PCM_FORMAT_FLOAT_LE) ||
	   (rtformat == SNDRV_PCM_FORMAT_FLOAT_BE))
		format = AVB_AVTP_AAF_FORMAT_32_BIT_FLOAT;
	else if((rtformat == SNDRV_PCM_FORMAT_S32_LE) ||
		(rtformat == SNDRV_PCM_FORMAT_S32_BE) ||
		(rtformat == SNDRV_PCM_FORMAT_U32_LE) ||
		(rtformat == SNDRV_PCM_FORMAT_U32_BE))
		format = AVB_AVTP_AAF_FORMAT_32_BIT_INT;
	else if((rtformat == SNDRV_PCM_FORMAT_S24_LE) ||
		(rtformat == SNDRV_PCM_FORMAT_S24_BE) ||
		(rtformat == SNDRV_PCM_FORMAT_U24_LE) ||
		(rtformat == SNDRV_PCM_FORMAT_U24_BE))
		format = AVB_AVTP_AAF_FORMAT_24_bit_INT;
	else if((rtformat == SNDRV_PCM_FORMAT_S16_LE) ||
		(rtformat == SNDRV_PCM_FORMAT_S16_BE) ||
		(rtformat == SNDRV_PCM_FORMAT_U16_LE) ||
		(rtformat == SNDRV_PCM_FORMAT_U16_BE))
		format = AVB_AVTP_AAF_FORMAT_16_BIT_INT;
	else
		format = AVB_AVTP_AAF_FORMAT_USER_SP;

	return format;
}

static int avb_get_avtp_aaf_nsr(int sampleRate)
{
	int nsr = AVB_AVTP_AAF_NSR_USER_SP;

	if(sampleRate == 8000)
		nsr = AVB_AVTP_AAF_NSR_8_KHZ;
	else if(sampleRate == 16000)
		nsr = AVB_AVTP_AAF_NSR_16_KHZ;
	else if(sampleRate == 32000)
		nsr = AVB_AVTP_AAF_NSR_32_KHZ;
	else if(sampleRate == 44100)
		nsr = AVB_AVTP_AAF_NSR_44_1_KHZ;
	else if(sampleRate == 48000)
		nsr = AVB_AVTP_AAF_NSR_48_KHZ;
	else if(sampleRate == 88200)
		nsr = AVB_AVTP_AAF_NSR_88_2_KHZ;
	else if(sampleRate == 96000)
		nsr = AVB_AVTP_AAF_NSR_96_KHZ;
	else if(sampleRate == 176400)
		nsr = AVB_AVTP_AAF_NSR_176_4_KHZ;
	else if(sampleRate == 192000)
		nsr = AVB_AVTP_AAF_NSR_192_KHZ;
	else if(sampleRate == 24000)
		nsr = AVB_AVTP_AAF_NSR_24_KHZ;
	else
		nsr = AVB_AVTP_AAF_NSR_USER_SP;

	return nsr;
}

static void avb_avtp_aaf_header_init(char* buf, struct snd_pcm_substream *substream, struct snd_pcm_hw_params *hw_params)
{
	struct avbcard *avbcard = snd_pcm_substream_chip(substream);
	struct ethhdr *eh = (struct ethhdr *)&buf[0];
	struct avtPduAafPcmHdr* hdr = (struct avtPduAafPcmHdr*)&buf[sizeof(struct ethhdr)];

	avb_log(AVB_KERN_INFO, KERN_INFO "avb_avtp_aaf_header_init");

	memset(buf, 0, AVB_MAX_ETH_FRAME_SIZE);

	eh->h_dest[0] = avbcard->sd.destmac[0];
	eh->h_dest[1] = avbcard->sd.destmac[1];
	eh->h_dest[2] = avbcard->sd.destmac[2];
	eh->h_dest[3] = avbcard->sd.destmac[3];
	eh->h_dest[4] = avbcard->sd.destmac[4];
	eh->h_dest[5] = avbcard->sd.destmac[5];
	eh->h_source[0] = avbcard->sd.srcmac[0];
	eh->h_source[1] = avbcard->sd.srcmac[1];
	eh->h_source[2] = avbcard->sd.srcmac[2];
	eh->h_source[3] = avbcard->sd.srcmac[3];
	eh->h_source[4] = avbcard->sd.srcmac[4];
	eh->h_source[5] = avbcard->sd.srcmac[5];

	eh->h_proto = htons(avbcard->sd.type);

	hdr->h.f.subType = AVB_AVTP_SUBTYPE_AAF;
	AVB_AVTP_AAF_HDR_SET_SV(hdr, 1);
	AVB_AVTP_AAF_HDR_SET_VER(hdr, AVB_AVTP_AAF_VERSION);
	AVB_AVTP_AAF_HDR_SET_MR(hdr, 0);
	AVB_AVTP_AAF_HDR_SET_TSV(hdr, 1);
	hdr->h.f.seqNo = 0;
	AVB_AVTP_AAF_HDR_SET_TU(hdr, 0);
	hdr->h.f.streamId = 0;
	hdr->h.f.avtpTS = 0;
	hdr->h.f.format = avb_get_avtp_aaf_format(substream->runtime->format);
	AVB_AVTP_AAF_HDR_SET_NSR(hdr, avb_get_avtp_aaf_nsr(params_rate(hw_params)));
	AVB_AVTP_AAF_HDR_SET_CPF(hdr, params_channels(hw_params));
	hdr->h.f.bitDepth = substream->runtime->sample_bits;
	hdr->h.f.streamDataLen = 0;
	AVB_AVTP_AAF_HDR_SET_SP(hdr, 1);
	AVB_AVTP_AAF_HDR_SET_EVT(hdr, 0);
}

static int avb_playback_open(struct snd_pcm_substream *substream)
{
	avb_log(AVB_KERN_NOT, KERN_NOTICE "avb_playback_open");

        substream->runtime->hw = avb_playback_hw;

	return 0;
}

static int avb_playback_close(struct snd_pcm_substream *substream)
{
	avb_log(AVB_KERN_NOT, KERN_NOTICE "avb_playback_close");

	return 0;
}

static int avb_playback_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *hw_params)
{
	struct avbcard *avbcard = snd_pcm_substream_chip(substream);

	avb_log(AVB_KERN_NOT, KERN_NOTICE "avb_playback_hw_params numbytes:%d sr:%d", params_buffer_bytes(hw_params), params_rate(hw_params));

	avbcard->tx.substream = substream;
	avbcard->tx.st = 0;
	avbcard->tx.sr = params_rate(hw_params);
	avbcard->tx.hwIdx = 0;
	avbcard->tx.seqNo = 0;
	avbcard->tx.hwnwIdx = 0;
	avbcard->tx.fillsize = 0;
	avbcard->tx.lastTimerTs = jiffies;
	avbcard->tx.socketCount = 0;
	avbcard->tx.pendingTxFrames = 0;
	avbcard->tx.numBytesConsumed = 0;
	avbcard->tx.periodsize = params_period_size(hw_params);
	avbcard->tx.buffersize = params_buffer_bytes(hw_params);
	avbcard->tx.framecount = params_buffer_size(hw_params);
	avbcard->tx.framesize  = params_buffer_bytes(hw_params) / params_buffer_size(hw_params);

	avb_avtp_aaf_header_init(&avbcard->sd.txBuf[0], substream, hw_params);

#ifdef AVB_USE_HIGH_RES_TIMER
	hrtimer_init((struct hrtimer*)&avbdevice.txTimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	avbdevice.txTimer.timer.function = &avb_avtp_timer;
	avbdevice.txTimer.card = avbcard;
#else
	init_timer(&avbdevice.txTimer);
	avbdevice.txTimer.data     = (unsigned long)avbcard;
	avbdevice.txTimer.function = avb_avtp_timer;
	avbdevice.txTimer.expires  = jiffies + 1;
	add_timer(&avbdevice.txTimer);
#endif

	memset(&avbdevice.txts[0], 0, (sizeof(int) * AVB_MAX_TS_SLOTS));
	avbdevice.txIdx = 0;

	avbcard->tx.tmpbuf = kmalloc(avbcard->tx.buffersize, GFP_KERNEL);

	return 0;
}

static int avb_playback_hw_free(struct snd_pcm_substream *substream)
{
	struct avbcard *avbcard = snd_pcm_substream_chip(substream);

	avb_log(AVB_KERN_NOT, KERN_NOTICE "avb_playback_hw_free");

#ifdef AVB_USE_HIGH_RES_TIMER
	 hrtimer_try_to_cancel((struct hrtimer*)&avbdevice.txTimer);
#else
	del_timer(&avbdevice.txTimer);
#endif	
	kfree(avbcard->tx.tmpbuf);

	return 0;
}

static int avb_playback_prepare(struct snd_pcm_substream *substream)
{
	return 0;
}

static int avb_playback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	ktime_t kt;
	int ret = 0;
	int avtpMaxFramesPerPacket = 0;
	struct avbcard *avbcard = snd_pcm_substream_chip(substream);

        switch (cmd) {
        case SNDRV_PCM_TRIGGER_START:
		avbcard->tx.st = 1;
                avb_log(AVB_KERN_NOT, KERN_NOTICE "avb_playback_trigger: Start @ %lu", jiffies);
#ifdef AVB_USE_HIGH_RES_TIMER
		avtpMaxFramesPerPacket = ((ETH_DATA_LEN - sizeof(struct avtPduAafPcmHdr)) / avbcard->tx.framesize);
		avbcard->tx.timerVal = ((avtpMaxFramesPerPacket * 1000000u) / (avbcard->tx.sr / 1000));
		kt = ktime_set(0, avbcard->tx.timerVal);
		hrtimer_start((struct hrtimer*)&avbdevice.txTimer, kt, HRTIMER_MODE_REL);
#else
		avbcard->tx.startts = jiffies;
		if((avbcard->tx.pendingTxFrames > 0) && (!timer_pending(&avbdevice.txTimer))) {
			avbdevice.txTimer.expires  = jiffies + 1;
			add_timer(&avbdevice.txTimer);
		}
#endif
                break;
        case SNDRV_PCM_TRIGGER_STOP:
                avb_log(AVB_KERN_NOT, KERN_NOTICE "avb_playback_trigger: Stop @ %lu", jiffies);
		avbcard->tx.st = 0;
#ifdef AVB_USE_HIGH_RES_TIMER
		 hrtimer_try_to_cancel((struct hrtimer*)&avbdevice.txTimer);
#endif
                break;
        default:
		avb_log(AVB_KERN_WARN, KERN_WARNING "avb_playback_trigger: Unknown");
                ret = -EINVAL;
        }

        return ret;
}

static snd_pcm_uframes_t avb_playback_pointer(struct snd_pcm_substream *substream)
{
	struct avbcard *avbcard = snd_pcm_substream_chip(substream);

	avb_log(AVB_KERN_INFO, KERN_INFO "avb_playback_pointer hwIdx:%lu numBytes:%lu, time: %u us",
		avbcard->tx.hwIdx, avbcard->tx.numBytesConsumed, jiffies_to_usecs(jiffies - avbcard->tx.startts));

	return avbcard->tx.hwIdx;
}

static int avb_playback_copy(struct snd_pcm_substream *substream, int channel,
			 unsigned long pos, void __user *buf,
			 unsigned long bytes)
{
	int err = 0;
	struct avbcard *avbcard = snd_pcm_substream_chip(substream);

	avb_log(AVB_KERN_INFO, KERN_INFO "avb_playback_copy: ch:%d, pos: %ld, count: %lu", channel, pos, bytes);

	if((err = copy_from_user(&avbcard->tx.tmpbuf[(pos * avbcard->tx.framesize)], buf, (bytes * avbcard->tx.framesize))) != 0) {
		avb_log(AVB_KERN_WARN, KERN_WARNING "avb_playback_copy copy from user fails: %d \n", err);
		return -1;
	}

	avbcard->tx.pendingTxFrames  += bytes;
	avbcard->tx.numBytesConsumed += (bytes * (substream->runtime->frame_bits / 8));

#ifndef AVB_USE_HIGH_RES_TIMER
	if(avbcard->tx.st == 1) {
		avb_avtp_timer((unsigned long)avbcard);
	}
	if((avbcard->tx.pendingTxFrames > 0) && (!timer_pending(&avbdevice.txTimer) && (avbcard->tx.st == 1))) {
		avbdevice.txTimer.expires  = jiffies + 1;
		add_timer(&avbdevice.txTimer);
	}
#endif

	return 0;
}

#ifdef AVB_USE_HIGH_RES_TIMER
enum hrtimer_restart avb_avtp_timer(struct hrtimer* t)
#else
static void avb_avtp_timer(unsigned long arg)
#endif
{
	ktime_t kt;
	int i = 0;
	int err = 0;
	int txSize = 0;
	snd_pcm_uframes_t bytesAvai = 0;
	snd_pcm_uframes_t bytesToCopy  = 0;
	snd_pcm_uframes_t framesToCopy = 0;
	snd_pcm_uframes_t avtpFramesPerPacket = 0;
	snd_pcm_uframes_t avtpMaxFramesPerPacket = 0;
	enum hrtimer_restart hrRes = HRTIMER_NORESTART;
	struct kvec vec;

#ifdef AVB_USE_HIGH_RES_TIMER
	struct avbcard *avbcard = ((struct avbhrtimer *)t)->card;
#else
	struct avbcard *avbcard = (struct avbcard *)arg;
#endif
	struct avtPduAafPcmHdr* hdr = (struct avtPduAafPcmHdr*)&avbcard->sd.txBuf[sizeof(struct ethhdr)];
#ifndef AVB_USE_HIGH_RES_TIMER
	unsigned long int numJiffies = ((jiffies > avbcard->tx.lastTimerTs)?(jiffies - avbcard->tx.lastTimerTs):(1));
	snd_pcm_uframes_t frameCount = ((avbcard->tx.sr * numJiffies) / HZ);
#endif

	avtpMaxFramesPerPacket = ((ETH_DATA_LEN - sizeof(struct avtPduAafPcmHdr)) / avbcard->tx.framesize);

#ifdef AVB_USE_HIGH_RES_TIMER
	avbcard->tx.accumframecount += avtpMaxFramesPerPacket;
	avtpFramesPerPacket = avtpMaxFramesPerPacket;
	kt = ktime_set(0, avbcard->tx.timerVal);
	avb_log(AVB_KERN_INFO, KERN_INFO "avb_avtp_timer mfppk: %lu, fppk: %lu, frSz: %lu, sr: %lu, time: %lu",
		avtpMaxFramesPerPacket, avtpFramesPerPacket, avbcard->tx.framesize, avbcard->tx.sr, avbcard->tx.timerVal);
#else
	avbcard->tx.accumframecount += frameCount;
	avtpFramesPerPacket = (avbcard->tx.sr / HZ);
	avtpFramesPerPacket = ((avtpFramesPerPacket > avtpMaxFramesPerPacket)?(avtpMaxFramesPerPacket):(avtpFramesPerPacket));
	avb_log(AVB_KERN_INFO, KERN_INFO "avb_avtp_timer ct: %lu, mfppk: %lu, fppk: %lu, frSz: %lu, sr: %lu, HZ: %lu",
		frameCount, avtpMaxFramesPerPacket, avtpFramesPerPacket, avbcard->tx.framesize, avbcard->tx.sr, HZ);
#endif

	while(((avbcard->tx.accumframecount >= avtpFramesPerPacket) ||
	       (avbcard->tx.pendingTxFrames <= avtpFramesPerPacket)) &&
#ifdef AVB_USE_HIGH_RES_TIMER
	      ((avbcard->tx.pendingTxFrames > 0) && (i < 1))) { 
#else
	      ((avbcard->tx.pendingTxFrames > 0) && (frameCount > 0) && (i < 32))) { 
#endif
		i++; /* Just as a failsafe to quit loop */
		avbcard->tx.seqNo++;
		hdr->h.f.seqNo = avbcard->tx.seqNo;

		txSize = sizeof(struct ethhdr) + sizeof(struct avtPduAafPcmHdr);
		framesToCopy = ((avbcard->tx.pendingTxFrames > avtpFramesPerPacket)?(avtpFramesPerPacket):(avbcard->tx.pendingTxFrames));
		bytesToCopy  = (framesToCopy * avbcard->tx.framesize);

		bytesAvai = ((avbcard->tx.framecount - avbcard->tx.hwIdx) * avbcard->tx.framesize);
		bytesAvai = ((bytesAvai >= bytesToCopy)?(bytesToCopy):(bytesAvai));

		memcpy(&avbcard->sd.txBuf[txSize], &avbcard->tx.tmpbuf[(avbcard->tx.hwIdx * avbcard->tx.framesize)], bytesAvai);

		if(bytesAvai < bytesToCopy) {
			memcpy(&avbcard->sd.txBuf[txSize+bytesAvai], &avbcard->tx.tmpbuf[0], (bytesToCopy - bytesAvai));
		}

		hdr->h.f.avtpTS = avbdevice.txts[((avbcard->tx.hwnwIdx / avbcard->tx.periodsize) % AVB_MAX_TS_SLOTS)];
		hdr->h.f.streamDataLen = bytesToCopy;
		txSize += bytesToCopy;

		avbcard->sd.txiov.iov_base = avbcard->sd.txBuf;
		avbcard->sd.txiov.iov_len  = txSize;

		vec.iov_base = avbcard->sd.txiov.iov_base;
		vec.iov_len = avbcard->sd.txiov.iov_len;

		iov_iter_init(&avbcard->sd.txMsgHdr.msg_iter, WRITE, &avbcard->sd.txiov, 1, txSize);

		if ((err = kernel_sendmsg(avbcard->sd.sock, &avbcard->sd.txMsgHdr, &vec, 1, txSize)) <= 0) {
			avb_log(AVB_KERN_WARN, KERN_WARNING "avb_avtp_timer Socket transmission fails %d \n", err);
			goto end;
		}

		avbcard->tx.accumframecount = ((avbcard->tx.accumframecount > framesToCopy)?(avbcard->tx.accumframecount - framesToCopy):(0));

		avbcard->tx.hwIdx += framesToCopy;
		avbcard->tx.hwnwIdx += framesToCopy;
		avbcard->tx.fillsize += framesToCopy;
		avbcard->tx.hwIdx = ((avbcard->tx.hwIdx < avbcard->tx.framecount)?(avbcard->tx.hwIdx):(avbcard->tx.hwIdx % avbcard->tx.framecount));
		avbcard->tx.pendingTxFrames -= framesToCopy;

		avb_log(AVB_KERN_INFO, KERN_INFO "avb_avtp_timer seqNo:%d, hwIdx: %lu, afrCt: %lu, penFrs:%lu, filSz:%lu",
			hdr->h.f.seqNo, avbcard->tx.hwIdx, avbcard->tx.accumframecount, avbcard->tx.pendingTxFrames, avbcard->tx.fillsize);

		if(avbcard->tx.fillsize >= avbcard->tx.periodsize) {
			avbcard->tx.fillsize %= avbcard->tx.periodsize;
			snd_pcm_period_elapsed(avbcard->tx.substream);
		}
	}

end:
#ifdef AVB_USE_HIGH_RES_TIMER
	if(avbcard->tx.st == 1) {
		hrtimer_forward_now(t, kt);
		hrRes = HRTIMER_RESTART;	
	}

	return hrRes;
#else
	if((avbcard->tx.pendingTxFrames > 0) && (!timer_pending(&avbdevice.txTimer))) {
		avbdevice.txTimer.expires  = jiffies + 1;
		add_timer(&avbdevice.txTimer);
	}
	avbcard->tx.lastTimerTs = jiffies;
#endif
}

static int avb_capture_open(struct snd_pcm_substream *substream)
{
	avb_log(AVB_KERN_NOT, KERN_NOTICE "avb_capture_open");

        substream->runtime->hw = avb_capture_hw;

	return 0;
}

static int avb_capture_close(struct snd_pcm_substream *substream)
{
	avb_log(AVB_KERN_NOT, KERN_NOTICE "avb_capture_close");

	return 0;
}

static int avb_capture_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *hw_params)
{
	struct avbcard *avbcard = snd_pcm_substream_chip(substream);

	avbcard->rx.substream = substream;
	avbcard->rx.hwIdx = 0;
	avbcard->rx.seqNo = 0;
	avbcard->rx.hwnwIdx = 0;
	avbcard->rx.fillsize = 0;
	avbcard->rx.prevHwIdx = 0;
	avbcard->rx.socketCount = 0;
	avbcard->rx.numBytesConsumed = 0;
	avbcard->rx.periodsize = params_period_size(hw_params);
	avbcard->rx.buffersize = params_buffer_bytes(hw_params);
	avbcard->rx.framecount = params_buffer_size(hw_params);
	avbcard->rx.framesize  = params_buffer_bytes(hw_params) / params_buffer_size(hw_params);

	avb_log(AVB_KERN_NOT, KERN_NOTICE "avb_capture_hw_params buffersize:%lu framesize:%lu", avbcard->rx.buffersize, avbcard->rx.framesize);

	memset(&avbdevice.rxts[0], 0, (sizeof(int) * AVB_MAX_TS_SLOTS));
	avbdevice.rxIdx = 0;

	avbdevice.avtpwd = avb_init_and_queue_work(AVB_DELAY_WORK_AVTP, (void*)avbcard, 1);

	avbcard->rx.tmpbuf = kmalloc(avbcard->rx.buffersize, GFP_KERNEL);

	return 0;
}

static int avb_capture_hw_free(struct snd_pcm_substream *substream)
{
	struct avbcard *avbcard = snd_pcm_substream_chip(substream);

	avb_log(AVB_KERN_NOT, KERN_NOTICE "avb_capture_hw_free");

	if(avbdevice.avtpwd != NULL) {
		cancel_delayed_work((struct delayed_work*)avbdevice.avtpwd);
		kfree(avbdevice.avtpwd);
		avbdevice.avtpwd = NULL;
	}

	kfree(avbcard->rx.tmpbuf);

	return 0;
}

static int avb_capture_prepare(struct snd_pcm_substream *substream)
{
	return 0;
}

static int avb_capture_trigger(struct snd_pcm_substream *substream, int cmd)
{
	int ret = 0;
	struct avbcard *avbcard = snd_pcm_substream_chip(substream);

        switch (cmd) {
        case SNDRV_PCM_TRIGGER_START:
                avb_log(AVB_KERN_NOT, KERN_NOTICE "avb_capture_trigger: Start @ %lu", jiffies);
		avbcard->rx.startts = jiffies;
                break;
        case SNDRV_PCM_TRIGGER_STOP:
                avb_log(AVB_KERN_NOT, KERN_NOTICE "avb_capture_trigger: Stop");
                break;
        default:
		avb_log(AVB_KERN_WARN, KERN_WARNING "avb_capture_trigger: Unknown");
                ret = -EINVAL;
        }

        return ret;
}

static snd_pcm_uframes_t avb_capture_pointer(struct snd_pcm_substream *substream)
{
	struct avbcard *avbcard = snd_pcm_substream_chip(substream);

	avb_log(AVB_KERN_INFO, KERN_INFO "avb_capture_pointer hwIdx:%lu numBytes:%lu",
		avbcard->rx.hwIdx, avbcard->rx.numBytesConsumed);

	return avbcard->rx.hwIdx;
}

static int avb_capture_copy(struct snd_pcm_substream *substream, int channel,
			 unsigned long pos, void __user *buf,
			 unsigned long bytes)
{
	char* srcbuf;
	int copyres = 0;
	struct avbcard *avbcard = snd_pcm_substream_chip(substream);

	srcbuf = (char*)&avbcard->rx.tmpbuf[pos * avbcard->rx.framesize];
	
	copyres = copy_to_user(buf, srcbuf, (bytes * avbcard->rx.framesize));

	avb_log(AVB_KERN_INFO, KERN_INFO "avb_capture_copy: ch:%d, pos: %ld, ct: %ld, res: %d", channel, pos, bytes, copyres);

	avbcard->rx.numBytesConsumed += (bytes * avbcard->rx.framesize);

	return bytes;
}

static int avb_avtp_listen(struct avbcard* avbcard)
{
	int err = 0;
	char* srcbuf;
	char* destbuf;
	int rxOff = 0;
	int rxSize = 0;
	int nrxSize = 0;
	int avaiSize = 0;
	int rxFrames = 0;
	int nrxFrames = 0;
	int nextSeqNo = 0;
	int skippedPackets = 0;
	mm_segment_t oldfs;
	snd_pcm_uframes_t hwIdx = 0;
	struct avtPduAafPcmHdr* hdr = (struct avtPduAafPcmHdr*)&avbcard->sd.rxBuf[sizeof(struct ethhdr)];
	struct kvec vec;

	memset(avbcard->sd.rxBuf, 0, AVB_MAX_ETH_FRAME_SIZE);
	avbcard->sd.rxiov.iov_base = avbcard->sd.rxBuf;
	avbcard->sd.rxiov.iov_len = AVB_MAX_ETH_FRAME_SIZE;
	vec.iov_base = avbcard->sd.rxiov.iov_base;
	vec.iov_len = avbcard->sd.rxiov.iov_len;
	iov_iter_init(&avbcard->sd.rxMsgHdr.msg_iter, READ, &avbcard->sd.rxiov, 1, AVB_MAX_ETH_FRAME_SIZE);

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = kernel_recvmsg(avbcard->sd.sock, &avbcard->sd.rxMsgHdr, &vec, 1, avbcard->sd.rxiov.iov_len, 0);
	set_fs(oldfs);

	if (err > 0) {
		avbcard->rx.socketCount++;

		rxOff   = sizeof(struct ethhdr) + sizeof(struct avtPduAafPcmHdr);
		srcbuf  = (char*)&avbcard->sd.rxBuf[rxOff];

		rxSize = hdr->h.f.streamDataLen;
		rxFrames = (rxSize / avbcard->rx.framesize);

		avbdevice.rxts[((avbcard->rx.hwnwIdx / avbcard->rx.periodsize) % AVB_MAX_TS_SLOTS)] = hdr->h.f.avtpTS;

		nextSeqNo = (avbcard->rx.seqNo + 1) % 256;

		if(nextSeqNo != hdr->h.f.seqNo) {
			avb_log(AVB_KERN_INFO, KERN_ERR "avb_listen missing frames from %d to %d \n",
				avbcard->rx.seqNo, hdr->h.f.seqNo);

			skippedPackets = ((hdr->h.f.seqNo >= avbcard->rx.seqNo)? \
						(hdr->h.f.seqNo - avbcard->rx.seqNo): \
						((hdr->h.f.seqNo + 255) - avbcard->rx.seqNo));
			nrxFrames = (skippedPackets * rxFrames);
			nrxSize = nrxFrames * avbcard->rx.framesize;

			avb_log(AVB_KERN_INFO, KERN_INFO "avb_listen idx: %ld nrsz: %d, nrf: %d \n",
				avbcard->rx.hwIdx, nrxSize, nrxFrames);

			avaiSize = ((avbcard->rx.framecount - avbcard->rx.hwIdx) * avbcard->rx.framesize);
			avaiSize = ((avaiSize < nrxSize)?(avaiSize):(nrxSize));
			destbuf  = (char*)&avbcard->rx.tmpbuf[avbcard->rx.hwIdx * avbcard->rx.framesize];
			memset(destbuf, 0, avaiSize);

			if(avaiSize < nrxSize) {
				destbuf  = (char*)&avbcard->rx.tmpbuf[0];
				memset(destbuf, 0, (nrxSize - avaiSize));
			}

			hwIdx = ((avbcard->rx.hwIdx + nrxFrames) % (avbcard->rx.framecount));
			rxFrames = rxFrames + nrxFrames;
		} else {
			hwIdx = avbcard->rx.hwIdx;
		}

		avb_log(AVB_KERN_INFO, KERN_INFO "avb_listen (%d) seq: %d, idx: %ld, sz: %d, ts: %u, rf: %d \n",
			avbcard->rx.socketCount, hdr->h.f.seqNo, hwIdx, rxSize, hdr->h.f.avtpTS, rxFrames);

		avbcard->rx.seqNo = hdr->h.f.seqNo;

		avaiSize = ((avbcard->rx.framecount - hwIdx) * avbcard->rx.framesize);
		avaiSize = ((avaiSize < rxSize)?(avaiSize):(rxSize));
		destbuf  = (char*)&avbcard->rx.tmpbuf[hwIdx * avbcard->rx.framesize];
		memcpy(destbuf, srcbuf, avaiSize);

		if(avaiSize < rxSize) {
			destbuf  = (char*)&avbcard->rx.tmpbuf[0];
			memcpy(destbuf, &srcbuf[avaiSize], (rxSize - avaiSize));
		}
	} else {
		if(err != -11)
			avb_log(AVB_KERN_WARN, KERN_WARNING "avb_avtp_listen Socket reception fails %d \n", err);
		return 0;
	}

	return rxFrames;
}

static bool avb_avdecc_init(struct avdecc* avdecc)
{
	avb_log(AVB_KERN_INFO, KERN_INFO "avb_avdecc_init");

	avdecc->sd.type = ETH_P_TSN;
	avdecc->sd.destmac[0] = 0x91;
	avdecc->sd.destmac[1] = 0xe0;
	avdecc->sd.destmac[2] = 0xf0;
	avdecc->sd.destmac[3] = 0x01;
	avdecc->sd.destmac[4] = 0x00;
	avdecc->sd.destmac[5] = 0x00;

	avdecc->acmpTxState = AVB_ACMP_STATUS_NOT_CONNECTED;
	avdecc->acmpRxState = AVB_ACMP_STATUS_NOT_CONNECTED;

	return avb_socket_init(&avdecc->sd, 1000);
}

static void avb_acdecc_initAndFillEthHdr(struct avdecc* avdecc, u8 multicast)
{
	struct ethhdr *eh = (struct ethhdr *)&avdecc->sd.txBuf[0];
	struct ethhdr *reh = (struct ethhdr *)&avdecc->sd.rxBuf[0];

	/* Initialize it */
	memset(avdecc->sd.txBuf, 0, AVB_MAX_ETH_FRAME_SIZE);

	/* Fill in the Ethernet header */
	if(multicast == 0) {
		eh->h_dest[0] = reh->h_source[0];
		eh->h_dest[1] = reh->h_source[1];
		eh->h_dest[2] = reh->h_source[2];
		eh->h_dest[3] = reh->h_source[3];
		eh->h_dest[4] = reh->h_source[4];
		eh->h_dest[5] = reh->h_source[5];
	} else {
		eh->h_dest[0] = 0x91;
		eh->h_dest[1] = 0xe0;
		eh->h_dest[2] = 0xf0;
		eh->h_dest[3] = 0x01;
		eh->h_dest[4] = 0x00;
		eh->h_dest[5] = 0x00;
	}
	eh->h_source[0] = avdecc->sd.srcmac[0];
	eh->h_source[1] = avdecc->sd.srcmac[1];
	eh->h_source[2] = avdecc->sd.srcmac[2];
	eh->h_source[3] = avdecc->sd.srcmac[3];
	eh->h_source[4] = avdecc->sd.srcmac[4];
	eh->h_source[5] = avdecc->sd.srcmac[5];

	/* Fill in Ethertype field */
	eh->h_proto = htons(avdecc->sd.type);
}

static void avb_acdecc_fillAVTPCtrlHdr(struct avdecc* avdecc, u8 subType, u8 msgType, u8 status, u16 dataLen, int streamId)
{
	struct avtPduControlHdr *hdr = (struct avtPduControlHdr*)&avdecc->sd.txBuf[sizeof(struct ethhdr)];

	hdr->h.f.subType = subType;
	AVB_AVTPDU_CTRL_HDR_SET_SV(hdr, 0);
	AVB_AVTPDU_CTRL_HDR_SET_VER(hdr, 0);
	AVB_AVTPDU_CTRL_HDR_SET_MSGTYPE(hdr, msgType);
	AVB_AVTPDU_CTRL_HDR_SET_VALIDTIME(hdr, status);
	AVB_AVTPDU_CTRL_HDR_SET_DATALEN(hdr, dataLen);
	if(streamId == 0) {
		hdr->h.f.streamId[0] = avdecc->sd.srcmac[0]; //avdecc->sd.srcmac[0];
		hdr->h.f.streamId[1] = avdecc->sd.srcmac[1]; //avdecc->sd.srcmac[1];
		hdr->h.f.streamId[2] = avdecc->sd.srcmac[2]; //avdecc->sd.srcmac[2];
		hdr->h.f.streamId[3] = avdecc->sd.srcmac[3]; //0xff;
		hdr->h.f.streamId[4] = avdecc->sd.srcmac[4]; //0xfe;
		hdr->h.f.streamId[5] = avdecc->sd.srcmac[5]; //avdecc->sd.srcmac[3];
		hdr->h.f.streamId[6] = 0; //avdecc->sd.srcmac[4];
		hdr->h.f.streamId[7] = 1; //avdecc->sd.srcmac[5];
	} else if (streamId > 0) {
		hdr->h.f.streamId[0] = avdecc->sd.srcmac[0];
		hdr->h.f.streamId[1] = avdecc->sd.srcmac[1];
		hdr->h.f.streamId[2] = avdecc->sd.srcmac[2];
		hdr->h.f.streamId[3] = 0xff;
		hdr->h.f.streamId[4] = 0xfe;
		hdr->h.f.streamId[5] = avdecc->sd.srcmac[3];
		hdr->h.f.streamId[6] = avdecc->sd.srcmac[4];
		hdr->h.f.streamId[7] = avdecc->sd.srcmac[5];
	} else {
		hdr->h.f.streamId[0] = 0;
		hdr->h.f.streamId[1] = 0;
		hdr->h.f.streamId[2] = 0;
		hdr->h.f.streamId[3] = 0;
		hdr->h.f.streamId[4] = 0;
		hdr->h.f.streamId[5] = 0;
		hdr->h.f.streamId[6] = 0;
		hdr->h.f.streamId[7] = 0;
	}
}

static void avb_maap_announce(struct avdecc* avdecc)
{
	int txSize = 0;
	int err = 0;
	struct kvec vec;
	
	struct maapPdu* pdu = (struct maapPdu*)&avdecc->sd.txBuf[sizeof(struct ethhdr)];

	avb_log(AVB_KERN_INFO, KERN_INFO "avb_maap_announce");

	avb_acdecc_initAndFillEthHdr(avdecc, 1);
	avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_MAAP, 3, 1, 16, -1);

	pdu->reqMAC[0] = 0x91;
	pdu->reqMAC[1] = 0xe0;
	pdu->reqMAC[2] = 0xf0;
	pdu->reqMAC[3] = 0x00;
	pdu->reqMAC[4] = 0x33;
	pdu->reqMAC[5] = 0x4b;

        pdu->reqCount = 2;

	txSize = sizeof(struct ethhdr) + sizeof(struct maapPdu);

	avdecc->sd.txiov.iov_base = avdecc->sd.txBuf;
	avdecc->sd.txiov.iov_len = txSize;

	vec.iov_base = avdecc->sd.txBuf;
	vec.iov_len = txSize;

	iov_iter_init(&avdecc->sd.txMsgHdr.msg_iter, WRITE, &avdecc->sd.txiov, 1, txSize);

	if ((err = kernel_sendmsg(avdecc->sd.sock, &avdecc->sd.txMsgHdr, &vec, 1, txSize)) <= 0) {
		avb_log(AVB_KERN_WARN, KERN_WARNING "avb_maap_announce Socket transmission fails %d \n", err);
		return;
	}
}

static void avb_adp_discover(struct avdecc* avdecc)
{
	int txSize = 0;
	int err = 0;
	struct kvec vec;

	avb_log(AVB_KERN_INFO, KERN_INFO "avb_adp_discover");

	avb_acdecc_initAndFillEthHdr(avdecc, 1);
	avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_ADP, AVB_ADP_MSGTYPE_ENTITY_DISCOVER, 31, AVB_ADP_CONTROL_DATA_LENGTH, 1);

	txSize = sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + AVB_ADP_CONTROL_DATA_LENGTH;

	avdecc->sd.txiov.iov_base = avdecc->sd.txBuf;
	avdecc->sd.txiov.iov_len = txSize;

	vec.iov_base = avdecc->sd.txBuf;
	vec.iov_len = txSize;

	iov_iter_init(&avdecc->sd.txMsgHdr.msg_iter, WRITE, &avdecc->sd.txiov, 1, txSize);

	if ((err = kernel_sendmsg(avdecc->sd.sock, &avdecc->sd.txMsgHdr, &vec, 1, txSize)) <= 0) {
		avb_log(AVB_KERN_WARN, KERN_WARNING "avb_adp_discover Socket transmission fails %d \n", err);
		return;
	}
}

static void avb_adp_advertise(struct avdecc* avdecc)
{
	int txSize = 0;
	int err = 0;
	struct kvec vec;
	struct adpdu* adpdu = (struct adpdu*)&avdecc->sd.txBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr)];

	avb_log(AVB_KERN_INFO, KERN_INFO "avb_adp_advertise");

	avb_acdecc_initAndFillEthHdr(avdecc, 1);
	avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_ADP, AVB_ADP_MSGTYPE_ENTITY_AVAILABLE, 31, AVB_ADP_CONTROL_DATA_LENGTH, 1);

	adpdu->entityModelId[0] = avdecc->sd.srcmac[0];
	adpdu->entityModelId[1] = avdecc->sd.srcmac[1];
	adpdu->entityModelId[2] = avdecc->sd.srcmac[2];
	adpdu->entityModelId[3] = avdecc->sd.srcmac[3];
	adpdu->entityModelId[4] = avdecc->sd.srcmac[4];
	adpdu->entityModelId[5] = avdecc->sd.srcmac[5];
	adpdu->entityModelId[6] = 0x00;
	adpdu->entityModelId[7] = 0x01;
	adpdu->entityCaps = avb_change_to_big_endian(0x00008508);
	adpdu->talkerStreamSources = avb_change_to_big_endian_u16(1);
	adpdu->talkerCaps = avb_change_to_big_endian_u16(0x4001);
	adpdu->listenerStreamSinks = avb_change_to_big_endian_u16(1);
	adpdu->listenerCaps = avb_change_to_big_endian_u16(0x4001);
	adpdu->controlCaps = 0;
	adpdu->avaiIdx = avb_change_to_big_endian(avdecc->adpAvaiIdx++);
	adpdu->gptpGrandMasterId[0] = /* 0x8d; */ 0xa8; //avdecc->sd.srcmac[0];
	adpdu->gptpGrandMasterId[1] = /* 0x85; */ 0x60; //avdecc->sd.srcmac[1];
	adpdu->gptpGrandMasterId[2] = /* 0x90; */ 0xb6; //avdecc->sd.srcmac[2];
	adpdu->gptpGrandMasterId[3] = /* 0x2c; */ 0xFF;
	adpdu->gptpGrandMasterId[4] = /* 0xdf; */ 0xFE;
	adpdu->gptpGrandMasterId[5] = /* 0xe9; */ 0x14; //avdecc->sd.srcmac[3];
	adpdu->gptpGrandMasterId[6] = /* 0x00; */ 0xa6; //avdecc->sd.srcmac[4];
	adpdu->gptpGrandMasterId[7] = /* 0x00; */ 0x3f; //avdecc->sd.srcmac[5];
	adpdu->gptpDomainNumber = 0;
	adpdu->idenCtrlIdx = 0;
	adpdu->interfaceIdx = 0;

	txSize = sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + AVB_ADP_CONTROL_DATA_LENGTH;

	avdecc->sd.txiov.iov_base = avdecc->sd.txBuf;
	avdecc->sd.txiov.iov_len = txSize;

	vec.iov_base = avdecc->sd.txBuf;
	vec.iov_len = txSize;

	iov_iter_init(&avdecc->sd.txMsgHdr.msg_iter, WRITE, &avdecc->sd.txiov, 1, txSize);

	if ((err = kernel_sendmsg(avdecc->sd.sock, &avdecc->sd.txMsgHdr, &vec, 1, txSize)) <= 0) {
		avb_log(AVB_KERN_WARN, KERN_WARNING "avb_adp_advertise Socket transmission fails %d \n", err);
		return;
	}
}

static int avb_avdecc_listen(struct avdecc* avdecc)
{
	int err = 0;
	mm_segment_t oldfs;
	struct kvec vec;

	memset(avdecc->sd.rxBuf, 0, AVB_MAX_ETH_FRAME_SIZE);
	avdecc->sd.rxiov.iov_base = avdecc->sd.rxBuf;
	avdecc->sd.rxiov.iov_len = AVB_MAX_ETH_FRAME_SIZE;

	vec.iov_base = avdecc->sd.rxiov.iov_base;
	vec.iov_len = avdecc->sd.rxiov.iov_len;

	iov_iter_init(&avdecc->sd.rxMsgHdr.msg_iter, READ, &avdecc->sd.rxiov, 1, AVB_MAX_ETH_FRAME_SIZE);

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = kernel_recvmsg(avdecc->sd.sock, &avdecc->sd.rxMsgHdr, &vec, 1 , avdecc->sd.rxiov.iov_len, 0);
	set_fs(oldfs);
	
	if (err <= 0)
		if(err != -11)
			avb_log(AVB_KERN_WARN, KERN_WARNING "avb_adecc_listen Socket reception res %d \n", err);
	
	return err;
}

static void avb_avdecc_aecp_respondToAEMCmd(struct avdecc* avdecc)
{
	int i = 0;
	int err = 0;
	u16 txSize = 0;
	int descpSize = 0;
	int maxCfgIdx = 0;
	int maxDescIdx = 0;
	struct aemCmd* cmd = (struct aemCmd*)&avdecc->sd.rxBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr)];

	struct readDescpCmd* rdCmd = (struct readDescpCmd*)cmd;
	struct getStreamInfoCmd* gtStrInfoCmd = (struct getStreamInfoCmd*)cmd;
	struct readDescpRes* rdRes = (struct readDescpRes*)&avdecc->sd.txBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr)];
	struct kvec vec;

	avb_acdecc_initAndFillEthHdr(avdecc, 0);
	
	memcpy(&rdRes->hdr.ctrlEntityId[0], &rdCmd->hdr.ctrlEntityId[0], 8);
	rdRes->hdr.seqId = rdCmd->hdr.seqId;
	rdRes->hdr.cmdType = cmd->cmdType;

	switch(avb_change_to_big_endian_u16(cmd->cmdType)) {
		case AVB_AEM_CMD_READ_DESCP: {
			
			rdRes->cfgIdx = rdCmd->cfgIdx;
			rdRes->res = 0;
			
			switch(avb_change_to_big_endian_u16(rdCmd->descType)) {
				case AVB_AEM_DESCP_ENTITY: {
					struct entityDescp* entDescp = (struct entityDescp*)&avdecc->sd.txBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct readDescpRes)];
					
					avb_log(AVB_KERN_INFO, KERN_INFO "avb_aecp_readResponse for Entity Descriptor");

					avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_SUCCESS, (sizeof(struct readDescpRes) + sizeof(struct entityDescp)), 1);
					entDescp->descType = avb_change_to_big_endian_u16(AVB_AEM_DESCP_ENTITY);
					entDescp->descIdx  = 0;
					entDescp->entityId[0] = avdecc->sd.srcmac[0];
					entDescp->entityId[1] = avdecc->sd.srcmac[1];
					entDescp->entityId[2] = avdecc->sd.srcmac[2];
					entDescp->entityId[3] = 0xff;
					entDescp->entityId[4] = 0xfe;
					entDescp->entityId[5] = avdecc->sd.srcmac[3];
					entDescp->entityId[6] = avdecc->sd.srcmac[4];
					entDescp->entityId[7] = avdecc->sd.srcmac[5];
					entDescp->entityModelId[0] = avdecc->sd.srcmac[0];
					entDescp->entityModelId[1] = avdecc->sd.srcmac[1];
					entDescp->entityModelId[2] = avdecc->sd.srcmac[2];
					entDescp->entityModelId[3] = avdecc->sd.srcmac[3];
					entDescp->entityModelId[4] = avdecc->sd.srcmac[4];
					entDescp->entityModelId[5] = avdecc->sd.srcmac[5];
					entDescp->entityModelId[6] = 0x00;
					entDescp->entityModelId[7] = 0x01;
					entDescp->entityCaps = avb_change_to_big_endian(0x00008508);
					entDescp->talkerStreamSources = avb_change_to_big_endian_u16(1);
					entDescp->talkerCaps = avb_change_to_big_endian_u16(0x4001);
					entDescp->listenerStreamSinks = avb_change_to_big_endian_u16(1);
					entDescp->listenerCaps = avb_change_to_big_endian_u16(0x4001);
					entDescp->controlCaps = 0;
					entDescp->avaiIdx = avb_change_to_big_endian(avdecc->adpAvaiIdx);
					strcpy(&entDescp->entityName[0], "ALSA_AVB_Driver");
					strcpy(&entDescp->firmwareVer[0], "0.1");
					strcpy(&entDescp->groupName[0], "0");
					strcpy(&entDescp->serialNumber[0], "0");
					entDescp->vendorNameString = avb_change_to_big_endian_u16(0x0000);
					entDescp->modelNameString = avb_change_to_big_endian_u16(0x0001);
					entDescp->cfgCount = avb_change_to_big_endian_u16(1);
					entDescp->currCfg = avb_change_to_big_endian_u16(0);
					descpSize = sizeof(struct entityDescp);
					break;
				} case AVB_AEM_DESCP_CONFIGURATION: {
					struct configDescp* cfgDescp = (struct configDescp*)&avdecc->sd.txBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct readDescpRes)];
					
					avb_log(AVB_KERN_INFO, KERN_INFO "avb_aecp_readResponse for Configuration Descriptor");

					avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_SUCCESS, (sizeof(struct readDescpRes) + sizeof(struct configDescp)), 1);
					cfgDescp->descType = avb_change_to_big_endian_u16(AVB_AEM_DESCP_CONFIGURATION);
					cfgDescp->descIdx  = 0;
					cfgDescp->localizedDescp = avb_change_to_big_endian_u16(0xffff);
					cfgDescp->descpCount = avb_change_to_big_endian_u16(AVB_AEM_MAX_DESCP_COUNT);
					cfgDescp->descpOff = avb_change_to_big_endian_u16(74);

					cfgDescp->descps[0].descType = avb_change_to_big_endian_u16(AVB_AEM_DESCP_AUDIO_UNIT);
					cfgDescp->descps[0].descCount = avb_change_to_big_endian_u16(1);
					cfgDescp->descps[1].descType = avb_change_to_big_endian_u16(AVB_AEM_DESCP_STREAM_IP);
					cfgDescp->descps[1].descCount = avb_change_to_big_endian_u16(1);
					cfgDescp->descps[2].descType = avb_change_to_big_endian_u16(AVB_AEM_DESCP_STREAM_OP);
					cfgDescp->descps[2].descCount = avb_change_to_big_endian_u16(1);
					cfgDescp->descps[3].descType = avb_change_to_big_endian_u16(AVB_AEM_DESCP_AVBINTERFACE);
					cfgDescp->descps[3].descCount = avb_change_to_big_endian_u16(1);
					cfgDescp->descps[4].descType = avb_change_to_big_endian_u16(AVB_AEM_DESCP_CLOCKSOURCE);
					cfgDescp->descps[4].descCount = avb_change_to_big_endian_u16(2);
					cfgDescp->descps[5].descType = avb_change_to_big_endian_u16(AVB_AEM_DESCP_LOCALE);
					cfgDescp->descps[5].descCount = avb_change_to_big_endian_u16(1);
					cfgDescp->descps[6].descType = avb_change_to_big_endian_u16(AVB_AEM_DESCP_CLOCK_DOMAIN);
					cfgDescp->descps[6].descCount = avb_change_to_big_endian_u16(1);
					descpSize = sizeof(struct configDescp);
					break;
				} case AVB_AEM_DESCP_AUDIO_UNIT: {
					struct audioUnitDescp* auDescp = (struct audioUnitDescp*)&avdecc->sd.txBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct readDescpRes)];
					
					avb_log(AVB_KERN_INFO, KERN_INFO "avb_aecp_readResponse for Audiounit Descriptor");

					avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_SUCCESS, (sizeof(struct readDescpRes) + sizeof(struct audioUnitDescp)), 1);
					auDescp->descType = avb_change_to_big_endian_u16(AVB_AEM_DESCP_AUDIO_UNIT);
					auDescp->descIdx  = 0;
					auDescp->localizedDescp = avb_change_to_big_endian_u16(0x0001);

					auDescp->numStreamIp = avb_change_to_big_endian_u16(1);
					auDescp->numStreamOp = avb_change_to_big_endian_u16(1);
					auDescp->numExtIp = avb_change_to_big_endian_u16(8);
					auDescp->numExtOp = avb_change_to_big_endian_u16(8);
					auDescp->currentSamplingRate = avb_change_to_big_endian(48000);
					auDescp->samplingRatesOffset = avb_change_to_big_endian_u16(144);
					auDescp->samplingRatesCount = avb_change_to_big_endian_u16(6);
					auDescp->samplingRates[0] = avb_change_to_big_endian(44100);
					auDescp->samplingRates[1] = avb_change_to_big_endian(48000);
					auDescp->samplingRates[2] = avb_change_to_big_endian(88200);
					auDescp->samplingRates[3] = avb_change_to_big_endian(96000);
					auDescp->samplingRates[4] = avb_change_to_big_endian(176400);
					auDescp->samplingRates[5] = avb_change_to_big_endian(192000);
					descpSize = sizeof(struct audioUnitDescp);
					break;
				} case AVB_AEM_DESCP_STREAM_PORT_IP:
				  case AVB_AEM_DESCP_STREAM_PORT_OP: {
					struct streamPortDescp* stPortDescp = (struct streamPortDescp*)&avdecc->sd.txBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct readDescpRes)];
					
					if(avb_change_to_big_endian_u16(rdCmd->descType) == AVB_AEM_DESCP_STREAM_PORT_IP) 
					  avb_log(AVB_KERN_INFO, KERN_INFO "avb_aecp_readResponse for Stream Input Port (%d) Descriptor", avb_change_to_big_endian_u16(rdCmd->descIdx));
					else
					  avb_log(AVB_KERN_INFO, KERN_INFO "avb_aecp_readResponse for Stream Output Port (%d) Descriptor", avb_change_to_big_endian_u16(rdCmd->descIdx));

					avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_SUCCESS, (sizeof(struct readDescpRes) + sizeof(struct streamPortDescp)), 1);
					stPortDescp->descType = rdCmd->descType;
					stPortDescp->descIdx  = rdCmd->descIdx;

					stPortDescp->numClusters = avb_change_to_big_endian_u16(8);
					stPortDescp->numMaps = avb_change_to_big_endian_u16(1);
					stPortDescp->portFlags = avb_change_to_big_endian_u16(((avb_change_to_big_endian_u16(rdCmd->descType) == AVB_AEM_DESCP_STREAM_PORT_IP)?(0):(1)));
					stPortDescp->baseCluster = avb_change_to_big_endian_u16(((avb_change_to_big_endian_u16(rdCmd->descType) == AVB_AEM_DESCP_STREAM_PORT_IP)?(0):(8)));
					stPortDescp->baseMap = avb_change_to_big_endian_u16(((avb_change_to_big_endian_u16(rdCmd->descType) == AVB_AEM_DESCP_STREAM_PORT_IP)?(0):(1)));
					descpSize = sizeof(struct streamPortDescp);
					break;
				} case AVB_AEM_DESCP_EXT_PORT_IP:
				  case AVB_AEM_DESCP_EXT_PORT_OP: {
					struct extPortDescp* extPortDescp = (struct extPortDescp*)&avdecc->sd.txBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct readDescpRes)];
					
					if(avb_change_to_big_endian_u16(rdCmd->descType) == AVB_AEM_DESCP_EXT_PORT_IP) 
					  avb_log(AVB_KERN_INFO, KERN_INFO "avb_aecp_readResponse for External Input Port (%d) Descriptor", avb_change_to_big_endian_u16(rdCmd->descIdx));
					else
					  avb_log(AVB_KERN_INFO, KERN_INFO "avb_aecp_readResponse for External Output Port (%d) Descriptor", avb_change_to_big_endian_u16(rdCmd->descIdx));

					avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_SUCCESS, (sizeof(struct readDescpRes) + sizeof(struct extPortDescp)), 1);
					extPortDescp->descType = rdCmd->descType;
					extPortDescp->descIdx  = rdCmd->descIdx;

					extPortDescp->signalType = avb_change_to_big_endian_u16(((avb_change_to_big_endian_u16(rdCmd->descType) == AVB_AEM_DESCP_EXT_PORT_IP)?(AVB_AEM_DESCP_INVALID):(AVB_AEM_DESCP_AUDIO_CLUSTER)));
					extPortDescp->signalIdx = avb_change_to_big_endian_u16(((avb_change_to_big_endian_u16(rdCmd->descType) == AVB_AEM_DESCP_EXT_PORT_IP)?(0):(avb_change_to_big_endian_u16(rdCmd->descIdx))));
					descpSize = sizeof(struct extPortDescp); maxDescIdx = 7;
					break;
				} case AVB_AEM_DESCP_JACK_IP:
				  case AVB_AEM_DESCP_JACK_OP: {
					struct jackDescp* jackDescp = (struct jackDescp*)&avdecc->sd.txBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct readDescpRes)];
					
					if(avb_change_to_big_endian_u16(rdCmd->descType) == AVB_AEM_DESCP_JACK_IP) 
					  avb_log(AVB_KERN_INFO, KERN_INFO "avb_aecp_readResponse for Jack Input Port (%d) Descriptor", avb_change_to_big_endian_u16(rdCmd->descIdx));
					else
					  avb_log(AVB_KERN_INFO, KERN_INFO "avb_aecp_readResponse for Jack Output Port (%d) Descriptor", avb_change_to_big_endian_u16(rdCmd->descIdx));

					avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_SUCCESS, (sizeof(struct readDescpRes) + sizeof(struct jackDescp)), 1);
					jackDescp->descType = rdCmd->descType;
					jackDescp->descIdx  = rdCmd->descIdx;
					if(avb_change_to_big_endian_u16(rdCmd->descType) == AVB_AEM_DESCP_JACK_IP)
					  strcpy(&jackDescp->objName[0], "ip-");
					else
					  strcpy(&jackDescp->objName[0], "op-");
					
					jackDescp->objName[3] = 48 + avb_change_to_big_endian_u16(rdCmd->descIdx);
					jackDescp->localizedDescp = avb_change_to_big_endian_u16(0x0007);

					jackDescp->jackType = avb_change_to_big_endian_u16(8); /* Balanced analog */
					descpSize = sizeof(struct jackDescp); maxDescIdx = 7;
					break;
				} case AVB_AEM_DESCP_AUDIO_CLUSTER: {
					struct audioClusterDescp* auClDescp = (struct audioClusterDescp*)&avdecc->sd.txBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct readDescpRes)];
					avb_log(AVB_KERN_INFO, KERN_INFO "avb_aecp_readResponse for Audio Cluster (%d) Descriptor", avb_change_to_big_endian_u16(rdCmd->descIdx));

					avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_SUCCESS, (sizeof(struct readDescpRes) + sizeof(struct audioClusterDescp)), 1);
					auClDescp->descType = rdCmd->descType;
					auClDescp->descIdx  = rdCmd->descIdx;

					auClDescp->localizedDescp = avb_change_to_big_endian_u16(0xffff);

					auClDescp->signalType = avb_change_to_big_endian_u16(((avb_change_to_big_endian_u16(rdCmd->descIdx) < 8)?(AVB_AEM_DESCP_INVALID):(AVB_AEM_DESCP_EXT_PORT_IP)));
					auClDescp->signalIdx = avb_change_to_big_endian_u16(((avb_change_to_big_endian_u16(rdCmd->descIdx) < 8)?(0):(avb_change_to_big_endian_u16(rdCmd->descIdx) - 8)));
					auClDescp->numChannels = avb_change_to_big_endian_u16(1);
					auClDescp->format = 0x40;
					descpSize = sizeof(struct audioClusterDescp); maxDescIdx = 15;
					break;
				} case AVB_AEM_DESCP_AUDIO_MAP: {
					struct audioMapDescp* auMapDescp = (struct audioMapDescp*)&avdecc->sd.txBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct readDescpRes)];
					avb_log(AVB_KERN_INFO, KERN_INFO "avb_aecp_readResponse for Audio Map (%d) Descriptor", avb_change_to_big_endian_u16(rdCmd->descIdx));

					avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_SUCCESS, (sizeof(struct readDescpRes) + sizeof(struct audioMapDescp)), 1);
					auMapDescp->descType = rdCmd->descType;
					auMapDescp->descIdx  = rdCmd->descIdx;

					auMapDescp->mappingOffset = avb_change_to_big_endian_u16(8);
					auMapDescp->numMappings = avb_change_to_big_endian_u16(8);

					for(i = 0; i < 8; i++) {
					  auMapDescp->map[i].streamIdx = avb_change_to_big_endian_u16(0);
					  auMapDescp->map[i].streamChannel = avb_change_to_big_endian_u16(i);
					  auMapDescp->map[i].clusterOffset = avb_change_to_big_endian_u16(i);
					  auMapDescp->map[i].clusterChannel = avb_change_to_big_endian_u16(0);
					}
					descpSize = sizeof(struct audioMapDescp); maxDescIdx = 1;
					break;
				} case AVB_AEM_DESCP_CLOCKSOURCE: {
					struct clockSourceDescp* clkSrcDescp = (struct clockSourceDescp*)&avdecc->sd.txBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct readDescpRes)];
					avb_log(AVB_KERN_INFO, KERN_INFO "avb_aecp_readResponse for Clock Source (%d) Descriptor", avb_change_to_big_endian_u16(rdCmd->descIdx));

					avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_SUCCESS, (sizeof(struct readDescpRes) + sizeof(struct clockSourceDescp)), 1);
					clkSrcDescp->descType = rdCmd->descType;
					clkSrcDescp->descIdx  = rdCmd->descIdx;

					clkSrcDescp->localizedDescp = avb_change_to_big_endian_u16(0x0004 + avb_change_to_big_endian_u16(rdCmd->descIdx));

					clkSrcDescp->clockSourceId[0] = avdecc->sd.srcmac[0];
					clkSrcDescp->clockSourceId[1] = avdecc->sd.srcmac[1];
					clkSrcDescp->clockSourceId[2] = avdecc->sd.srcmac[2];
					clkSrcDescp->clockSourceId[3] = avdecc->sd.srcmac[3];
					clkSrcDescp->clockSourceId[4] = avdecc->sd.srcmac[4];
					clkSrcDescp->clockSourceId[5] = avdecc->sd.srcmac[5];
					clkSrcDescp->clockSourceId[6] = 0;
					clkSrcDescp->clockSourceId[7] = (u8)avb_change_to_big_endian_u16(rdCmd->descIdx);

					if(avb_change_to_big_endian_u16(rdCmd->descIdx) == 0) {
					  clkSrcDescp->clockSourcFlags = avb_change_to_big_endian_u16(0x00);
					  clkSrcDescp->clockSourceType = avb_change_to_big_endian_u16(0x00);
					  clkSrcDescp->clockSourceLocType = avb_change_to_big_endian_u16(AVB_AEM_DESCP_AUDIO_UNIT);
					} else if(avb_change_to_big_endian_u16(rdCmd->descIdx) == 1) {
					  clkSrcDescp->clockSourcFlags = avb_change_to_big_endian_u16(0x00);
					  clkSrcDescp->clockSourceType = avb_change_to_big_endian_u16(0x01);
					  clkSrcDescp->clockSourceLocType = avb_change_to_big_endian_u16(AVB_AEM_DESCP_JACK_IP);
					} else {
					  clkSrcDescp->clockSourcFlags = avb_change_to_big_endian_u16(0x02);
					  clkSrcDescp->clockSourceType = avb_change_to_big_endian_u16(0x02);
					  clkSrcDescp->clockSourceLocType = avb_change_to_big_endian_u16(AVB_AEM_DESCP_STREAM_IP);
					}

					descpSize = sizeof(struct clockSourceDescp); maxDescIdx = 2;
					break;
				} case AVB_AEM_DESCP_CLOCK_DOMAIN: {
					struct clockDomainDescp* clkDoDescp = (struct clockDomainDescp*)&avdecc->sd.txBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct readDescpRes)];
					avb_log(AVB_KERN_INFO, KERN_INFO "avb_aecp_readResponse for Clock Domain (%d) Descriptor", avb_change_to_big_endian_u16(rdCmd->descIdx));

					avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_SUCCESS, (sizeof(struct readDescpRes) + sizeof(struct clockDomainDescp)), 1);
					clkDoDescp->descType = rdCmd->descType;
					clkDoDescp->descIdx  = rdCmd->descIdx;

					clkDoDescp->localizedDescp = avb_change_to_big_endian_u16(0x0000);

					clkDoDescp->currClockSource = avb_change_to_big_endian_u16(0x00);
					clkDoDescp->clockSourcesOffset = avb_change_to_big_endian_u16(0x4c);
					clkDoDescp->clockSourcesCount = avb_change_to_big_endian_u16(3);
					clkDoDescp->clockSources[0] = avb_change_to_big_endian_u16(0);
					clkDoDescp->clockSources[1] = avb_change_to_big_endian_u16(1);
					clkDoDescp->clockSources[2] = avb_change_to_big_endian_u16(2);

					descpSize = sizeof(struct clockDomainDescp);
					break;
				} case AVB_AEM_DESCP_AVBINTERFACE: {
					struct avbIfDescp* avbIfDescp = (struct avbIfDescp*)&avdecc->sd.txBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct readDescpRes)];
					
					avb_log(AVB_KERN_INFO, KERN_INFO "avb_aecp_readResponse for AVB Interface Descriptor");

					avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_SUCCESS, (sizeof(struct readDescpRes) + sizeof(struct avbIfDescp)), 1);
					avbIfDescp->descType = avb_change_to_big_endian_u16(AVB_AEM_DESCP_AVBINTERFACE);
					avbIfDescp->descIdx  = 0;
					avbIfDescp->localizedDescp = avb_change_to_big_endian_u16(0xffff);
					avbIfDescp->macAddr[0] = avdecc->sd.srcmac[0];
					avbIfDescp->macAddr[1] = avdecc->sd.srcmac[1];
					avbIfDescp->macAddr[2] = avdecc->sd.srcmac[2];
					avbIfDescp->macAddr[3] = avdecc->sd.srcmac[3];
					avbIfDescp->macAddr[4] = avdecc->sd.srcmac[4];
					avbIfDescp->macAddr[5] = avdecc->sd.srcmac[5];
					avbIfDescp->ifFlags = avb_change_to_big_endian_u16(0x0007);
					avbIfDescp->clockIden[0] = avdecc->sd.srcmac[0];
					avbIfDescp->clockIden[1] = avdecc->sd.srcmac[1];
					avbIfDescp->clockIden[2] = avdecc->sd.srcmac[2];
					avbIfDescp->clockIden[3] = 0xff;
					avbIfDescp->clockIden[4] = 0xfe;
					avbIfDescp->clockIden[5] = avdecc->sd.srcmac[3];
					avbIfDescp->clockIden[6] = avdecc->sd.srcmac[4];
					avbIfDescp->clockIden[7] = avdecc->sd.srcmac[5];
					avbIfDescp->prio1 = 250;
					avbIfDescp->clockClass = 248;
					avbIfDescp->offScaledLogVar = avb_change_to_big_endian_u16(0x4100);
					avbIfDescp->clockAccu = 254;
					avbIfDescp->prio2 = 250;
					avbIfDescp->domainNo = 0;
					avbIfDescp->logSyncInt = 15;
					avbIfDescp->logAnnoInt = 15;
					avbIfDescp->logPDelayInt = 13;
					avbIfDescp->portNo = avb_change_to_big_endian_u16(0x0001);
					descpSize = sizeof(struct avbIfDescp);
					break;
				} case AVB_AEM_DESCP_LOCALE: {
					struct localeDescp* localeDescp = (struct localeDescp*)&avdecc->sd.txBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct readDescpRes)];

					avb_log(AVB_KERN_INFO, KERN_INFO "avb_aecp_readResponse for Locale Descriptor");

					avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_SUCCESS, (sizeof(struct readDescpRes) + sizeof(struct localeDescp)), 1);

					localeDescp->descType = avb_change_to_big_endian_u16(AVB_AEM_DESCP_LOCALE);
					localeDescp->descIdx  = 0;
					strcpy(&localeDescp->localeId[0], "en-US");
					localeDescp->numStrings = avb_change_to_big_endian_u16(1);
					localeDescp->baseStringsIdx = avb_change_to_big_endian_u16(0);
					descpSize = sizeof(struct localeDescp);
					break;
				} case AVB_AEM_DESCP_STRINGS: {
					struct stringsDescp* stringsDescp = (struct stringsDescp*)&avdecc->sd.txBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct readDescpRes)];

					avb_log(AVB_KERN_INFO, KERN_INFO "avb_aecp_readResponse for Strings Descriptor");

					avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_SUCCESS, (sizeof(struct readDescpRes) + sizeof(struct stringsDescp)), 1);

					stringsDescp->descType = avb_change_to_big_endian_u16(AVB_AEM_DESCP_STRINGS);
					stringsDescp->descIdx  = 0;
					strcpy(&stringsDescp->strings[0][0], "FH-Kiel");
					strcpy(&stringsDescp->strings[1][0], "BBB");
					strcpy(&stringsDescp->strings[2][0], "Stream IP");
					strcpy(&stringsDescp->strings[3][0], "Stream OP");
					strcpy(&stringsDescp->strings[4][0], "Domain Clock");
					strcpy(&stringsDescp->strings[5][0], "External Clock");
					strcpy(&stringsDescp->strings[6][0], "Stream Clock");
					descpSize = sizeof(struct stringsDescp);
					break;
				} case AVB_AEM_DESCP_STREAM_IP: {
					struct streamDescp* strmOpDescp = (struct streamDescp*)&avdecc->sd.txBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct readDescpRes)];
					
					avb_log(AVB_KERN_INFO, KERN_INFO "avb_aecp_readResponse for Stream Out Descriptor");

					avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_SUCCESS, (sizeof(struct readDescpRes) + sizeof(struct streamDescp)), 1);
					strmOpDescp->descType = avb_change_to_big_endian_u16(AVB_AEM_DESCP_STREAM_IP);
					strmOpDescp->descIdx  = 0;
					strmOpDescp->localizedDescp = avb_change_to_big_endian_u16(0x0002);
					strmOpDescp->clockDomainIdx = 0;
					strmOpDescp->streamFlags = avb_change_to_big_endian_u16(0x0003);
					strmOpDescp->avbIfIdx = 0;
					strmOpDescp->bufSize = avb_change_to_big_endian(583333);
					strmOpDescp->fmtsCount = avb_change_to_big_endian_u16(AVB_AEM_MAX_SUPP_FORMATS);
					strmOpDescp->fmtsOff = avb_change_to_big_endian_u16(132);

#if 0
					strmOpDescp->suppFmts[0].fmt.avtp.subType  = AVB_AEM_STREAM_FORMAT_AVTP;
					strmOpDescp->suppFmts[0].fmt.avtp.b1.nsr   = 5;
					strmOpDescp->suppFmts[0].fmt.avtp.format   = 4;
					strmOpDescp->suppFmts[0].fmt.avtp.bitDepth = 16;
					strmOpDescp->suppFmts[0].fmt.avtp.cpf      = 2;
					strmOpDescp->suppFmts[0].fmt.avtp.b5.cpf   = 0;
					strmOpDescp->suppFmts[0].fmt.avtp.b5.spf   = strmOpDescp->suppFmts[0].fmt.avtp.b5.spf | 0x01;
					strmOpDescp->suppFmts[0].fmt.avtp.b6.spf   = 0;
					strmOpDescp->suppFmts[0].fmt.avtp.b6.res2  = 0;
					strmOpDescp->suppFmts[0].fmt.avtp.res2     = 0;
#else
					strmOpDescp->suppFmts[0].fmt.iec.subType = 0;
					strmOpDescp->suppFmts[0].fmt.iec.b1.sf = 0x80;
					strmOpDescp->suppFmts[0].fmt.iec.b1.fmt = strmOpDescp->suppFmts[0].fmt.iec.b1.fmt | 0x20;
					strmOpDescp->suppFmts[0].fmt.iec.b2.fdf_sfc = 0x01;
					strmOpDescp->suppFmts[0].fmt.iec.dbs = 0x08;
					strmOpDescp->suppFmts[0].fmt.iec.b4.b = 0x60;
					strmOpDescp->suppFmts[0].fmt.iec.label_mbla_cnt = 0x08;
#endif
					memcpy(&strmOpDescp->suppFmts[1], &strmOpDescp->suppFmts[0], sizeof(struct streamFormat));
					memcpy(&strmOpDescp->suppFmts[2], &strmOpDescp->suppFmts[0], sizeof(struct streamFormat));
					memcpy(&strmOpDescp->suppFmts[3], &strmOpDescp->suppFmts[0], sizeof(struct streamFormat));
					memcpy(&strmOpDescp->suppFmts[4], &strmOpDescp->suppFmts[0], sizeof(struct streamFormat));
					memcpy(&strmOpDescp->suppFmts[5], &strmOpDescp->suppFmts[0], sizeof(struct streamFormat));

					strmOpDescp->suppFmts[1].fmt.iec.b2.fdf_sfc = 0x02;
					strmOpDescp->suppFmts[2].fmt.iec.b2.fdf_sfc = 0x03;
					strmOpDescp->suppFmts[3].fmt.iec.b2.fdf_sfc = 0x04;
					strmOpDescp->suppFmts[4].fmt.iec.b2.fdf_sfc = 0x05;
					strmOpDescp->suppFmts[5].fmt.iec.b2.fdf_sfc = 0x06;

					memcpy(&strmOpDescp->currFmt, &strmOpDescp->suppFmts[1], sizeof(struct streamFormat));

					descpSize = sizeof(struct streamDescp);
					break;
				} case AVB_AEM_DESCP_STREAM_OP: {
					struct streamDescp* strmOpDescp = (struct streamDescp*)&avdecc->sd.txBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct readDescpRes)];
					
					avb_log(AVB_KERN_INFO, KERN_INFO "avb_aecp_readResponse for Stream Out Descriptor");

					avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_SUCCESS, (sizeof(struct readDescpRes) + sizeof(struct streamDescp)), 1);
					strmOpDescp->descType = avb_change_to_big_endian_u16(AVB_AEM_DESCP_STREAM_OP);
					strmOpDescp->descIdx  = 0;
					strmOpDescp->localizedDescp = avb_change_to_big_endian_u16(0x0003);
					strmOpDescp->clockDomainIdx = 0;
					strmOpDescp->streamFlags = avb_change_to_big_endian_u16(0x0002);
					strmOpDescp->avbIfIdx = 0;
					strmOpDescp->bufSize = avb_change_to_big_endian(583333);
					strmOpDescp->fmtsCount = avb_change_to_big_endian_u16(AVB_AEM_MAX_SUPP_FORMATS);
					strmOpDescp->fmtsOff = avb_change_to_big_endian_u16(132);

#if 0
					strmOpDescp->suppFmts[0].fmt.avtp.subType  = AVB_AEM_STREAM_FORMAT_AVTP;
					strmOpDescp->suppFmts[0].fmt.avtp.b1.nsr   = 5;
					strmOpDescp->suppFmts[0].fmt.avtp.format   = 4;
					strmOpDescp->suppFmts[0].fmt.avtp.bitDepth = 16;
					strmOpDescp->suppFmts[0].fmt.avtp.cpf      = 2;
					strmOpDescp->suppFmts[0].fmt.avtp.b5.cpf   = 0;
					strmOpDescp->suppFmts[0].fmt.avtp.b5.spf   = strmOpDescp->suppFmts[0].fmt.avtp.b5.spf | 0x01;
					strmOpDescp->suppFmts[0].fmt.avtp.b6.spf   = 0;
					strmOpDescp->suppFmts[0].fmt.avtp.b6.res2  = 0;
					strmOpDescp->suppFmts[0].fmt.avtp.res2     = 0;
#else
					strmOpDescp->suppFmts[0].fmt.iec.subType = 0;
					strmOpDescp->suppFmts[0].fmt.iec.b1.sf = 0x80;
					strmOpDescp->suppFmts[0].fmt.iec.b1.fmt = strmOpDescp->suppFmts[0].fmt.iec.b1.fmt | 0x20;
					strmOpDescp->suppFmts[0].fmt.iec.b2.fdf_sfc = 0x01;
					strmOpDescp->suppFmts[0].fmt.iec.dbs = 0x08;
					strmOpDescp->suppFmts[0].fmt.iec.b4.b = 0x60;
					strmOpDescp->suppFmts[0].fmt.iec.label_mbla_cnt = 0x08;
#endif
					memcpy(&strmOpDescp->suppFmts[1], &strmOpDescp->suppFmts[0], sizeof(struct streamFormat));
					memcpy(&strmOpDescp->suppFmts[2], &strmOpDescp->suppFmts[0], sizeof(struct streamFormat));
					memcpy(&strmOpDescp->suppFmts[3], &strmOpDescp->suppFmts[0], sizeof(struct streamFormat));
					memcpy(&strmOpDescp->suppFmts[4], &strmOpDescp->suppFmts[0], sizeof(struct streamFormat));
					memcpy(&strmOpDescp->suppFmts[5], &strmOpDescp->suppFmts[0], sizeof(struct streamFormat));

					strmOpDescp->suppFmts[1].fmt.iec.b2.fdf_sfc = 0x02;
					strmOpDescp->suppFmts[2].fmt.iec.b2.fdf_sfc = 0x03;
					strmOpDescp->suppFmts[3].fmt.iec.b2.fdf_sfc = 0x04;
					strmOpDescp->suppFmts[4].fmt.iec.b2.fdf_sfc = 0x05;
					strmOpDescp->suppFmts[5].fmt.iec.b2.fdf_sfc = 0x06;

					memcpy(&strmOpDescp->currFmt, &strmOpDescp->suppFmts[1], sizeof(struct streamFormat));

					descpSize = sizeof(struct streamDescp);
					break;
				} default: {
					struct readDescpCmd* rdResCmd = (struct readDescpCmd*)rdRes;
					rdResCmd->descType = rdCmd->descType;
					rdResCmd->descIdx  = rdCmd->descIdx;
					descpSize = 0;
					txSize = sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct readDescpCmd);
					avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_NOT_IMPLEMENTED, (sizeof(struct readDescpCmd)), 1);
					avb_log(AVB_KERN_INFO, KERN_INFO "avb_avdecc_aecp_respondToAEMCmd unknown descType: %d", avb_change_to_big_endian_u16(rdCmd->descType));
					break;
				}
			}
			

			if(descpSize > 0) {
				if((avb_change_to_big_endian_u16(rdCmd->cfgIdx) > maxCfgIdx) || (avb_change_to_big_endian_u16(rdCmd->descIdx) > maxDescIdx)) {
					struct readDescpCmd* rdResCmd = (struct readDescpCmd*)rdRes;
					rdResCmd->descType = rdCmd->descType;
					rdResCmd->descIdx  = rdCmd->descIdx;
					txSize = sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct readDescpCmd);
					avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_NO_SUCH_DESCRIPTOR, (sizeof(struct readDescpCmd)), 1);
				} else {
					txSize = sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct readDescpRes) + descpSize;
				}
			}

			break;
		}  case AVB_AEM_CMD_ENTITY_ACQUIRE: {
			struct acquireEntCmd* acqEntCmd = (struct acquireEntCmd*)&avdecc->sd.txBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr)];

			avb_log(AVB_KERN_INFO, KERN_INFO "avb_aecp_readResponse for Entiry Acquire");
			txSize = sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct acquireEntCmd);
			avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_SUCCESS, (sizeof(struct acquireEntCmd)), 1);

			memcpy(&acqEntCmd->ownerId[0], &acqEntCmd->hdr.ctrlEntityId[0], 8);
			acqEntCmd->flags = 0;
			acqEntCmd->descType = avb_change_to_big_endian_u16(AVB_AEM_DESCP_ENTITY);
			acqEntCmd->descIdx = 0;
			
			avbdevice.msrp.started = true;
			avb_msrp_talkerdeclarations(&avbdevice.msrp, true, avbdevice.msrp.txState);
			break;
		}  case AVB_AEM_CMD_ENTITY_AVAILABLE: {
			avb_log(AVB_KERN_INFO, KERN_INFO "avb_aecp_readResponse for Entity Available");
			txSize = sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct aemCmd);
			avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_SUCCESS, (sizeof(struct aemCmd)), 1);
			break;
		} case AVB_AEM_CMD_SET_STREAM_FORMAT: {
			struct setStreamFormatCmd* reqStFmt = (struct setStreamFormatCmd*)&avdecc->sd.rxBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr)]; 
			struct setStreamFormatCmd* stFmt = (struct setStreamFormatCmd*)&avdecc->sd.txBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr)]; 

			avb_log(AVB_KERN_INFO, KERN_INFO "avb_aecp_readResponse for Set Stream Format");
			txSize = sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct setStreamFormatCmd);
			avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_SUCCESS, (sizeof(struct setStreamFormatCmd)), 1);
			memcpy(stFmt, reqStFmt, sizeof(struct setStreamFormatCmd));
			break;
		} case AVB_AEM_CMD_GET_STREAM_INFO: {
			struct streamInfo* getStreamInfoReq = (struct streamInfo*)&avdecc->sd.rxBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr)];
			struct streamInfo* streamInfo = (struct streamInfo*)&avdecc->sd.txBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr)];
					
			avb_log(AVB_KERN_INFO, KERN_INFO "avb_aecp_readResponse for Get Stream Info command");

			avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_SUCCESS, sizeof(struct streamInfo), 1);
			streamInfo->descType = getStreamInfoReq->descType;
			streamInfo->descIdx  = 0;
			streamInfo->flags = avb_change_to_big_endian(0x0);
			streamInfo->currFmt.fmt.avtp.subType  = AVB_AEM_STREAM_FORMAT_AVTP;
			streamInfo->currFmt.fmt.avtp.b1.nsr   = 5;
			streamInfo->currFmt.fmt.avtp.format   = 4;
			streamInfo->currFmt.fmt.avtp.bitDepth = 16;
			streamInfo->currFmt.fmt.avtp.cpf      = 2;
			streamInfo->currFmt.fmt.avtp.b5.cpf   = 0;
			streamInfo->currFmt.fmt.avtp.b5.spf   = streamInfo->currFmt.fmt.avtp.b5.spf | 0x01;
			streamInfo->currFmt.fmt.avtp.b6.spf   = 0;
			streamInfo->currFmt.fmt.avtp.b6.res2  = 0;
			streamInfo->currFmt.fmt.avtp.res2     = 0;
			streamInfo->streamId[0] = ((avb_change_to_big_endian_u16(getStreamInfoReq->descType) == AVB_AEM_DESCP_STREAM_OP)?(avdecc->sd.srcmac[0]):(0));
			streamInfo->streamId[1] = ((avb_change_to_big_endian_u16(getStreamInfoReq->descType) == AVB_AEM_DESCP_STREAM_OP)?(avdecc->sd.srcmac[1]):(0));
			streamInfo->streamId[2] = ((avb_change_to_big_endian_u16(getStreamInfoReq->descType) == AVB_AEM_DESCP_STREAM_OP)?(avdecc->sd.srcmac[2]):(0));
			streamInfo->streamId[3] = ((avb_change_to_big_endian_u16(getStreamInfoReq->descType) == AVB_AEM_DESCP_STREAM_OP)?(avdecc->sd.srcmac[3]):(0));
			streamInfo->streamId[4] = ((avb_change_to_big_endian_u16(getStreamInfoReq->descType) == AVB_AEM_DESCP_STREAM_OP)?(avdecc->sd.srcmac[4]):(0));
			streamInfo->streamId[5] = ((avb_change_to_big_endian_u16(getStreamInfoReq->descType) == AVB_AEM_DESCP_STREAM_OP)?(avdecc->sd.srcmac[5]):(0));
			streamInfo->streamId[6] = ((avb_change_to_big_endian_u16(getStreamInfoReq->descType) == AVB_AEM_DESCP_STREAM_OP)?(0):(0));
			streamInfo->streamId[7] = ((avb_change_to_big_endian_u16(getStreamInfoReq->descType) == AVB_AEM_DESCP_STREAM_OP)?(1):(0));
			streamInfo->msrpAccuLat = avb_change_to_big_endian(0);
			streamInfo->streamDestMAC[0] = 0x91;
			streamInfo->streamDestMAC[1] = 0xe0;
			streamInfo->streamDestMAC[2] = 0xf0;
			streamInfo->streamDestMAC[3] = 0x00;
			streamInfo->streamDestMAC[4] = 0x3f;
			streamInfo->streamDestMAC[5] = 0xf1;
			streamInfo->msrpFailureCode = 0;
			streamInfo->streamVlanId = avb_change_to_big_endian_u16(2);

			if(gtStrInfoCmd->descIdx > 0) {
				txSize = sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct getStreamInfoCmd);
				avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_NO_SUCH_DESCRIPTOR, (sizeof(struct getStreamInfoCmd)), 1);
			} else {
				txSize = sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct streamInfo);
			}
			break;
		} case AVB_AEM_CMD_GET_COUNTERS: {
			struct countersDescp* countersReq = (struct countersDescp*)&avdecc->sd.rxBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr)];
			struct countersDescp* countersDescp = (struct countersDescp*)&avdecc->sd.txBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr)];
			
			avb_log(AVB_KERN_INFO, KERN_INFO "avb_aecp_readResponse for Get Counters command");

			avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_SUCCESS, sizeof(struct streamInfo), 1);

			countersDescp->descType = countersReq->descType;
			countersDescp->descIdx  = 0;
	
			if(countersReq->descType == AVB_AEM_DESCP_STREAM_IP) {
				countersDescp->countersValid = avb_change_to_big_endian(0x00001800);

				countersDescp->counters[11] = 0;
				countersDescp->counters[12] = 0;
			} else {
				countersDescp->countersValid = avb_change_to_big_endian(0x0);
			}				

			if(gtStrInfoCmd->descIdx > 0) {
				txSize = sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct getCountersCmd);
				avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_NO_SUCH_DESCRIPTOR, (sizeof(struct getCountersCmd)), 1);
			} else {
				txSize = sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct countersDescp);
			}
			break;		
		} case AVB_AEM_CMD_REGISTER_UNSOLICITED_NOTIFICATION: {
			avb_log(AVB_KERN_INFO, KERN_INFO "avb_aecp_readResponse for Register Unsolicited Responses");
			txSize = sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct aemCmd);
			avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_SUCCESS, (sizeof(struct aemCmd)), 1);
			break;
		} default: {
			txSize = sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct readDescpRes);
			avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_AECP, AVB_AECP_MSGTYPE_AEM_RESPONSE, AVB_AEM_RES_NOT_IMPLEMENTED, (sizeof(struct readDescpRes)), 1);
			avb_log(AVB_KERN_INFO, KERN_INFO "avb_avdecc_aecp_respondToAEMCmd unknown cmdType: %d", avb_change_to_big_endian_u16(cmd->cmdType));
			break;
		}
	}

	if(txSize > 0) {
		avdecc->sd.txiov.iov_base = avdecc->sd.txBuf;
		avdecc->sd.txiov.iov_len = txSize;

		vec.iov_base = avdecc->sd.txBuf;
		vec.iov_len = txSize;

		iov_iter_init(&avdecc->sd.txMsgHdr.msg_iter, WRITE, &avdecc->sd.txiov, 1, txSize);

		if ((err = kernel_sendmsg(avdecc->sd.sock, &avdecc->sd.txMsgHdr, &vec, 1, txSize)) <= 0) {
			avb_log(AVB_KERN_WARN, KERN_WARNING "avb_avdecc_aecp_respondToAEMCmd Socket transmission fails %d \n", err);
			return;
		}	
	}
}

static void avb_avdecc_aecp_respondToCmd(struct avdecc* avdecc)
{
	struct avtPduControlHdr *hdr = (struct avtPduControlHdr*)&avdecc->sd.rxBuf[sizeof(struct ethhdr)];

	switch(hdr->h.f.b1.msgType) {
		case AVB_AECP_MSGTYPE_AEM_COMMAND:
			avb_avdecc_aecp_respondToAEMCmd(avdecc);
			break;
		
		default:
			avb_log(AVB_KERN_INFO, KERN_INFO "avb_avdecc_aecp_respondToCmd unknown subType: %d", hdr->h.f.b1.msgType);
			break;
	}
}

static void avb_avdecc_acmp_connectTxCmd(struct avdecc* avdecc)
{
	int err = 0;
	u16 txSize = 0;
	struct acmPdu* txacmpdu = (struct acmPdu*)&avdecc->sd.txBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr)];
	struct kvec vec;

	avb_log(AVB_KERN_INFO, KERN_INFO "avb_avdecc_adp_respondToCmd Send Connect TX command");
	avb_acdecc_initAndFillEthHdr(avdecc, 1);
	avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_ACMP, AVB_ACMP_MSGTYPE_CONNECT_TX_CMD, AVB_ACMP_STATUS_SUCCESS, sizeof(struct acmPdu), 0);
	txacmpdu->streamDestMAC[0] = 0x91;
	txacmpdu->streamDestMAC[1] = 0xe0;
	txacmpdu->streamDestMAC[2] = 0xf0;
	txacmpdu->streamDestMAC[3] = 0x00;
	txacmpdu->streamDestMAC[4] = 0x33;
	txacmpdu->streamDestMAC[5] = 0x4b;
	txacmpdu->flags = avb_change_to_big_endian_u16(0x0000);
	txSize = sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct acmPdu);
	
	if(txSize > 0) {
		avdecc->sd.txiov.iov_base = avdecc->sd.txBuf;
		avdecc->sd.txiov.iov_len = txSize;

		vec.iov_base = avdecc->sd.txBuf;
		vec.iov_len = txSize;

		iov_iter_init(&avdecc->sd.txMsgHdr.msg_iter, WRITE, &avdecc->sd.txiov, 1, txSize);

		if ((err = kernel_sendmsg(avdecc->sd.sock, &avdecc->sd.txMsgHdr, &vec, 1, txSize)) <= 0) {
			avb_log(AVB_KERN_WARN, KERN_WARNING "avb_avdecc_acmp_respondToAEMCmd Socket transmission fails %d \n", err);
			return;
		}	
	}
}

static void avb_avdecc_acmp_connectRxCmd(struct avdecc* avdecc)
{
	int err = 0;
	u16 txSize = 0;
	struct acmPdu* txacmpdu = (struct acmPdu*)&avdecc->sd.txBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr)];
	struct kvec vec;

	avb_log(AVB_KERN_INFO, KERN_INFO "avb_avdecc_adp_respondToCmd Send Connect RX command");
	avb_acdecc_initAndFillEthHdr(avdecc, 1);
	avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_ACMP, AVB_ACMP_MSGTYPE_CONNECT_RX_CMD, AVB_ACMP_STATUS_SUCCESS, sizeof(struct acmPdu), 0);
	txacmpdu->streamDestMAC[0] = 0x91;
	txacmpdu->streamDestMAC[1] = 0xe0;
	txacmpdu->streamDestMAC[2] = 0xf0;
	txacmpdu->streamDestMAC[3] = 0x00;
	txacmpdu->streamDestMAC[4] = 0x33;
	txacmpdu->streamDestMAC[5] = 0x4c;
	txacmpdu->flags = avb_change_to_big_endian_u16(0x0000);
	txSize = sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct acmPdu);
	
	if(txSize > 0) {
		avdecc->sd.txiov.iov_base = avdecc->sd.txBuf;
		avdecc->sd.txiov.iov_len = txSize;

		vec.iov_base = avdecc->sd.txBuf;
		vec.iov_len = txSize;

		iov_iter_init(&avdecc->sd.txMsgHdr.msg_iter, WRITE, &avdecc->sd.txiov, 1, txSize);

		if ((err = kernel_sendmsg(avdecc->sd.sock, &avdecc->sd.txMsgHdr, &vec, 1, txSize)) <= 0) {
			avb_log(AVB_KERN_WARN, KERN_WARNING "avb_avdecc_acmp_respondToAEMCmd Socket transmission fails %d \n", err);
			return;
		}	
	}
}

static void avb_avdecc_acmp_respondToCmd(struct avdecc* avdecc)
{
	int err = 0;
	int dest = 0;
	u16 txSize = 0;
	struct avtPduControlHdr *hdr = (struct avtPduControlHdr*)&avdecc->sd.rxBuf[sizeof(struct ethhdr)];
	struct acmPdu* rxacmpdu = (struct acmPdu*)&avdecc->sd.rxBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr)];
	struct acmPdu* txacmpdu = (struct acmPdu*)&avdecc->sd.txBuf[sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr)];
	struct kvec vec;


	if((rxacmpdu->talkerEntityId[0] == avdecc->sd.srcmac[0]) &&
	   (rxacmpdu->talkerEntityId[1] == avdecc->sd.srcmac[1]) &&
	   (rxacmpdu->talkerEntityId[2] == avdecc->sd.srcmac[2]) &&
	   (rxacmpdu->talkerEntityId[3] == 0xff) &&
	   (rxacmpdu->talkerEntityId[4] == 0xfe) &&
	   (rxacmpdu->talkerEntityId[5] == avdecc->sd.srcmac[3]) &&
	   (rxacmpdu->talkerEntityId[6] == avdecc->sd.srcmac[4]) &&
	   (rxacmpdu->talkerEntityId[7] == avdecc->sd.srcmac[5])) {
		dest++;
	} else if((rxacmpdu->listenerEntityId[0] == avdecc->sd.srcmac[0]) &&
	   	  (rxacmpdu->listenerEntityId[1] == avdecc->sd.srcmac[1]) &&
	   	  (rxacmpdu->listenerEntityId[2] == avdecc->sd.srcmac[2]) &&
	   	  (rxacmpdu->listenerEntityId[3] == 0xff) &&
	   	  (rxacmpdu->listenerEntityId[4] == 0xfe) &&
	   	  (rxacmpdu->listenerEntityId[5] == avdecc->sd.srcmac[3]) &&
	   	  (rxacmpdu->listenerEntityId[6] == avdecc->sd.srcmac[4]) &&
	   	  (rxacmpdu->listenerEntityId[7] == avdecc->sd.srcmac[5])) {
		dest--;
	} else {
		dest = 0;
	}

	if(((hdr->h.f.b1.msgType == AVB_ACMP_MSGTYPE_GET_TX_STATE_CMD)  || (hdr->h.f.b1.msgType == AVB_ACMP_MSGTYPE_CONNECT_TX_CMD) ||
	    (hdr->h.f.b1.msgType == AVB_ACMP_MSGTYPE_DISCONNECT_TX_CMD) || (hdr->h.f.b1.msgType == AVB_ACMP_MSGTYPE_GET_TX_CONN_CMD)) && (dest <= 0)) {
		avb_log(AVB_KERN_INFO, KERN_INFO "avb_avdecc_aecp_respondToCmd ACMP TX cmd: %d not for us", hdr->h.f.b1.msgType);
		return;
	}

	if(((hdr->h.f.b1.msgType == AVB_ACMP_MSGTYPE_GET_RX_STATE_CMD) || (hdr->h.f.b1.msgType == AVB_ACMP_MSGTYPE_CONNECT_RX_CMD) ||
	    (hdr->h.f.b1.msgType == AVB_ACMP_MSGTYPE_DISCONNECT_RX_CMD)) && (dest >= 0)) {
		avb_log(AVB_KERN_INFO, KERN_INFO "avb_avdecc_aecp_respondToCmd ACMP RX cmd: %d not for us", hdr->h.f.b1.msgType);
		return;
	}

	switch(hdr->h.f.b1.msgType) {
		case AVB_ACMP_MSGTYPE_GET_TX_STATE_CMD:
			avb_log(AVB_KERN_INFO, KERN_INFO "avb_avdecc_adp_respondToCmd Get TX state command");
			avb_acdecc_initAndFillEthHdr(avdecc, 1);
			avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_ACMP, AVB_ACMP_MSGTYPE_GET_TX_STATE_RESP, avdecc->acmpTxState, sizeof(struct acmPdu), 0);
			memcpy(txacmpdu, rxacmpdu, sizeof(struct acmPdu));
			txacmpdu->streamDestMAC[0] = 0x91;
			txacmpdu->streamDestMAC[1] = 0xe0;
			txacmpdu->streamDestMAC[2] = 0xf0;
			txacmpdu->streamDestMAC[3] = 0x00;
			txacmpdu->streamDestMAC[4] = 0x33;
			txacmpdu->streamDestMAC[5] = 0x4b;
			txacmpdu->flags = avb_change_to_big_endian_u16(0x0000);
			txSize = sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct acmPdu);
			break;

		case AVB_ACMP_MSGTYPE_GET_RX_STATE_CMD:
			avb_log(AVB_KERN_INFO, KERN_INFO "avb_avdecc_adp_respondToCmd Get RX state command");
			avb_acdecc_initAndFillEthHdr(avdecc, 1);
			avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_ACMP, AVB_ACMP_MSGTYPE_GET_RX_STATE_RESP, avdecc->acmpRxState, sizeof(struct acmPdu), 0);
			memcpy(txacmpdu, rxacmpdu, sizeof(struct acmPdu));
			txacmpdu->streamDestMAC[0] = 0x91;
			txacmpdu->streamDestMAC[1] = 0xe0;
			txacmpdu->streamDestMAC[2] = 0xf0;
			txacmpdu->streamDestMAC[3] = 0x00;
			txacmpdu->streamDestMAC[4] = 0x33;
			txacmpdu->streamDestMAC[5] = 0x4c;
			txacmpdu->flags = avb_change_to_big_endian_u16(0x0000);
			txSize = sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct acmPdu);
			break;

		case AVB_ACMP_MSGTYPE_CONNECT_TX_CMD:
			avb_log(AVB_KERN_INFO, KERN_INFO "avb_avdecc_adp_respondToCmd Connect TX command");
			avb_acdecc_initAndFillEthHdr(avdecc, 1);
			avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_ACMP, AVB_ACMP_MSGTYPE_CONNECT_TX_RESP, AVB_ACMP_STATUS_SUCCESS, sizeof(struct acmPdu), 0);
			memcpy(txacmpdu, rxacmpdu, sizeof(struct acmPdu));
			txacmpdu->streamDestMAC[0] = 0x91;
			txacmpdu->streamDestMAC[1] = 0xe0;
			txacmpdu->streamDestMAC[2] = 0xf0;
			txacmpdu->streamDestMAC[3] = 0x00;
			txacmpdu->streamDestMAC[4] = 0x33;
			txacmpdu->streamDestMAC[5] = 0x4b;
			txacmpdu->flags = avb_change_to_big_endian_u16(0x0000);
			txSize = sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct acmPdu);
			break;

		case AVB_ACMP_MSGTYPE_DISCONNECT_TX_CMD:
			avb_log(AVB_KERN_INFO, KERN_INFO "avb_avdecc_adp_respondToCmd Disconnect TX command");
			avb_acdecc_initAndFillEthHdr(avdecc, 1);
			avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_ACMP, AVB_ACMP_MSGTYPE_DISCONNECT_TX_RESP, AVB_ACMP_STATUS_SUCCESS, sizeof(struct acmPdu), 0);
			memcpy(txacmpdu, rxacmpdu, sizeof(struct acmPdu));
			txacmpdu->flags = avb_change_to_big_endian_u16(0x0000);
			txSize = sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct acmPdu);
			break;

		case AVB_ACMP_MSGTYPE_CONNECT_RX_CMD:
			avb_log(AVB_KERN_INFO, KERN_INFO "avb_avdecc_adp_respondToCmd Connect RX command");
			avb_acdecc_initAndFillEthHdr(avdecc, 1);
			avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_ACMP, AVB_ACMP_MSGTYPE_CONNECT_RX_RESP, AVB_ACMP_STATUS_SUCCESS, sizeof(struct acmPdu), 0);
			memcpy(txacmpdu, rxacmpdu, sizeof(struct acmPdu));
			txacmpdu->streamDestMAC[0] = 0x91;
			txacmpdu->streamDestMAC[1] = 0xe0;
			txacmpdu->streamDestMAC[2] = 0xf0;
			txacmpdu->streamDestMAC[3] = 0x00;
			txacmpdu->streamDestMAC[4] = 0x33;
			txacmpdu->streamDestMAC[5] = 0x4c;
			txacmpdu->flags = avb_change_to_big_endian_u16(0x0000);
			txSize = sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct acmPdu);
			break;

		case AVB_ACMP_MSGTYPE_DISCONNECT_RX_CMD:
			avb_log(AVB_KERN_INFO, KERN_INFO "avb_avdecc_adp_respondToCmd Disconnect RX command");
			avb_acdecc_initAndFillEthHdr(avdecc, 1);
			avb_acdecc_fillAVTPCtrlHdr(avdecc, AVB_AVTP_SUBTYPE_ACMP, AVB_ACMP_MSGTYPE_DISCONNECT_RX_RESP, AVB_ACMP_STATUS_SUCCESS, sizeof(struct acmPdu), 0);
			memcpy(txacmpdu, rxacmpdu, sizeof(struct acmPdu));
			txacmpdu->flags = avb_change_to_big_endian_u16(0x0000);
			txSize = sizeof(struct ethhdr) + sizeof(struct avtPduControlHdr) + sizeof(struct acmPdu);
			break;

		default:
			avb_log(AVB_KERN_INFO, KERN_INFO "avb_avdecc_acmp_respondToCmd unknown subType: %d", hdr->h.f.b1.msgType);
			break;
	}

	if(txSize > 0) {
		avdecc->sd.txiov.iov_base = avdecc->sd.txBuf;
		avdecc->sd.txiov.iov_len = txSize;

		vec.iov_base = avdecc->sd.txBuf;
		vec.iov_len = txSize;

		iov_iter_init(&avdecc->sd.txMsgHdr.msg_iter, WRITE, &avdecc->sd.txiov, 1, txSize);

		if ((err = kernel_sendmsg(avdecc->sd.sock, &avdecc->sd.txMsgHdr, &vec, 1, txSize)) <= 0) {
			avb_log(AVB_KERN_WARN, KERN_WARNING "avb_avdecc_acmp_respondToAEMCmd Socket transmission fails %d \n", err);
			return;
		}	
	}
}

static void avb_avdecc_adp_respondToCmd(struct avdecc* avdecc)
{
	struct avtPduControlHdr *hdr = (struct avtPduControlHdr*)&avdecc->sd.rxBuf[sizeof(struct ethhdr)];

	switch(hdr->h.f.b1.msgType) {
		case AVB_ADP_MSGTYPE_ENTITY_AVAILABLE:
			avb_log(AVB_KERN_INFO, KERN_INFO "avb_avdecc_adp_respondToCmd Entity Available");
			break;
		
		case AVB_ADP_MSGTYPE_ENTITY_DEPARTING:
			avb_log(AVB_KERN_INFO, KERN_INFO "avb_avdecc_adp_respondToCmd Entity Departing");
			break;
		
		case AVB_ADP_MSGTYPE_ENTITY_DISCOVER:
			avb_log(AVB_KERN_INFO, KERN_INFO "avb_avdecc_adp_respondToCmd Entity Discover");
			break;

		default:
			avb_log(AVB_KERN_INFO, KERN_INFO "avb_avdecc_adp_respondToCmd unknown subType: %d", hdr->h.f.b1.msgType);
			break;
	}
}

static void avb_avdecc_listen_and_respond(struct avdecc* avdecc)
{
	struct avtPduControlHdr *hdr = (struct avtPduControlHdr*)&avdecc->sd.rxBuf[sizeof(struct ethhdr)];

	if(avb_avdecc_listen(avdecc) > 0) {

		switch(hdr->h.f.subType) {
			case AVB_AVTP_SUBTYPE_ADP:
				avb_avdecc_adp_respondToCmd(avdecc);
				break;

			case AVB_AVTP_SUBTYPE_AECP:
				avb_avdecc_aecp_respondToCmd(avdecc);
				break;

			case AVB_AVTP_SUBTYPE_ACMP:
				avb_avdecc_acmp_respondToCmd(avdecc);
				break;

			default:
				avb_log(AVB_KERN_INFO, KERN_INFO "avb_avdecc_listen_and_respond unknown subType: %d", hdr->h.f.subType);
				break;
		}
	}
}

static bool avb_msrp_init(struct msrp* msrp)
{
	avb_log(AVB_KERN_INFO, KERN_INFO "avb_msrp_init");

	msrp->rxState = MSRP_DECLARATION_STATE_NONE;
	msrp->txState = MSRP_DECLARATION_STATE_NONE;

	msrp->sd.type = ETH_MSRP;
	msrp->sd.destmac[0] = 0x01;
	msrp->sd.destmac[1] = 0x80;
	msrp->sd.destmac[2] = 0xC2;
	msrp->sd.destmac[3] = 0x00;
	msrp->sd.destmac[4] = 0x00;
	msrp->sd.destmac[5] = 0x0E;

	return avb_socket_init(&msrp->sd, 1000);
}

static void avb_msrp_domaindeclarations(struct msrp* msrp)
{
	int txSize = 0;
	int err = 0;
	struct ethhdr *eh = (struct ethhdr *)&msrp->sd.txBuf[0];
	struct domainmsrpdu *pdu = (struct domainmsrpdu*)&msrp->sd.txBuf[sizeof(struct ethhdr)];
	struct kvec vec;

	avb_log(AVB_KERN_INFO, KERN_INFO "avb_msrp_domaindeclarations");

	/* Initialize it */
	memset(msrp->sd.txBuf, 0, AVB_MAX_ETH_FRAME_SIZE);

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

	pdu->protocolversion = 0;
	pdu->msg.attributetype = MSRP_ATTRIBUTE_TYPE_DOMAIN_VECTOR;
	pdu->msg.attributelen  = MSRP_ATTRIBUTE_LEN_DOMAIN_VECTOR;
	pdu->msg.attributelistlen = avb_change_to_big_endian_u16(sizeof(struct domainvectorattribute) + 2);
	pdu->msg.attibutelist.hdr.numberofvalues = avb_change_to_big_endian_u16(1);
	pdu->msg.attibutelist.val.srClassId = 2;
	pdu->msg.attibutelist.val.srClassPrio = 3;
	pdu->msg.attibutelist.val.srClassVID = avb_change_to_big_endian_u16(2);
	pdu->msg.attibutelist.vector[0] = MSRP_THREE_PACK(MSRP_ATTRIBUTE_EVENT_JOININ, 0, 0);
	pdu->msg.endmarker = 0;
	pdu->endmarker = 0;

	txSize = sizeof(struct ethhdr) + sizeof(struct domainmsrpdu);

	msrp->sd.txiov.iov_base = msrp->sd.txBuf;
	msrp->sd.txiov.iov_len = txSize;

	vec.iov_base = msrp->sd.txiov.iov_base;
	vec.iov_len = msrp->sd.txiov.iov_len;

	iov_iter_init(&msrp->sd.txMsgHdr.msg_iter, WRITE, &msrp->sd.txiov, 1, txSize);

	if ((err = kernel_sendmsg(msrp->sd.sock, &msrp->sd.txMsgHdr, &vec, 1, txSize)) <= 0) {
		avb_log(AVB_KERN_WARN, KERN_WARNING "avb_msrp_domaindeclarations Socket transmission fails %d \n", err);
		return;
	}
}

static void avb_msrp_talkerdeclarations(struct msrp* msrp, bool join, int state)
{
	int txSize = 0;
	int err = 0;
	struct ethhdr *eh = (struct ethhdr *)&msrp->sd.txBuf[0];
	struct talkermsrpdu *pdu = (struct talkermsrpdu*)&msrp->sd.txBuf[sizeof(struct ethhdr)];
	struct kvec vec;

	avb_log(AVB_KERN_INFO, KERN_INFO "avb_msrp_talkerdeclarations join: %d, state: %d", join, state);

	/* Initialize it */
	memset(msrp->sd.txBuf, 0, AVB_MAX_ETH_FRAME_SIZE);

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

	pdu->protocolversion = 0;
	pdu->msg.attributetype = MSRP_ATTRIBUTE_TYPE_TALKER_ADVERTISE_VECTOR;
	pdu->msg.attributelen  = MSRP_ATTRIBUTE_LEN_TALKER_ADVERTISE_VECTOR;
	pdu->msg.attributelistlen = avb_change_to_big_endian_u16(sizeof(struct talkervectorattribute));

	if(state != MSRP_DECLARATION_STATE_UNKNOWN) {
		pdu->msg.attibutelist.hdr.numberofvalues = avb_change_to_big_endian_u16(1);
		pdu->msg.attibutelist.val.streamid[0] = msrp->sd.srcmac[0];
		pdu->msg.attibutelist.val.streamid[1] = msrp->sd.srcmac[1];
		pdu->msg.attibutelist.val.streamid[2] = msrp->sd.srcmac[2];
		pdu->msg.attibutelist.val.streamid[3] = msrp->sd.srcmac[3];
		pdu->msg.attibutelist.val.streamid[4] = msrp->sd.srcmac[4];
		pdu->msg.attibutelist.val.streamid[5] = msrp->sd.srcmac[5];
		pdu->msg.attibutelist.val.streamid[6] = 0;
		pdu->msg.attibutelist.val.streamid[7] = 1;

		pdu->msg.attibutelist.val.dataframeparams[0] = msrp->sd.destmac[0];
		pdu->msg.attibutelist.val.dataframeparams[1] = msrp->sd.destmac[1];
		pdu->msg.attibutelist.val.dataframeparams[2] = msrp->sd.destmac[2];
		pdu->msg.attibutelist.val.dataframeparams[3] = msrp->sd.destmac[3];
		pdu->msg.attibutelist.val.dataframeparams[4] = msrp->sd.destmac[4];
		pdu->msg.attibutelist.val.dataframeparams[5] = msrp->sd.destmac[5];
		pdu->msg.attibutelist.val.dataframeparams[6] = 0;
		pdu->msg.attibutelist.val.dataframeparams[7] = 2;

		pdu->msg.attibutelist.val.maxFrameSize = avb_change_to_big_endian_u16(MSRP_MAX_FRAME_SIZE_48KHZ_AUDIO);
		pdu->msg.attibutelist.val.maxintervalframes = avb_change_to_big_endian_u16(MSRP_MAX_INTERVAL_FRAME_48KHZ_AUDIO);
		pdu->msg.attibutelist.val.priorityandrank = 0;
		pdu->msg.attibutelist.val.accumalatedlatency = avb_change_to_big_endian(0);

		pdu->msg.attibutelist.vector[0] = MSRP_THREE_PACK(((join == true)?(MSRP_ATTRIBUTE_EVENT_JOININ):(MSRP_ATTRIBUTE_EVENT_LEAVE)), 0, 0);
	} else {
		pdu->msg.attibutelist.vector[0] = MSRP_THREE_PACK(MSRP_ATTRIBUTE_EVENT_JOINMT, 0, 0);
	}

	pdu->msg.endmarker = 0;
	pdu->endmarker = 0;

	txSize = sizeof(struct ethhdr) + sizeof(struct talkermsrpdu);

	msrp->sd.txiov.iov_base = msrp->sd.txBuf;
	msrp->sd.txiov.iov_len = txSize;

	vec.iov_base = msrp->sd.txiov.iov_base;
	vec.iov_len = msrp->sd.txiov.iov_len;

	iov_iter_init(&msrp->sd.txMsgHdr.msg_iter, WRITE, &msrp->sd.txiov, 1, txSize);

	if ((err = kernel_sendmsg(msrp->sd.sock, &msrp->sd.txMsgHdr, &vec, 1, txSize)) <= 0) {
		avb_log(AVB_KERN_WARN, KERN_WARNING "avb_msrp_talkerdeclarations Socket transmission fails %d \n", err);
		return;
	}
}

static void avb_msrp_listenerdeclarations(struct msrp* msrp, bool join, int state)
{
	int txSize = 0;
	int err = 0;
	struct ethhdr *eh = (struct ethhdr *)&msrp->sd.txBuf[0];
	struct listnermsrpdu *pdu = (struct listnermsrpdu*)&msrp->sd.txBuf[sizeof(struct ethhdr)];
	struct kvec vec;

	avb_log(AVB_KERN_INFO, KERN_INFO "avb_msrp_listenerdeclarations join: %d, state: %d", join, state);

	/* Initialize it */
	memset(msrp->sd.txBuf, 0, AVB_MAX_ETH_FRAME_SIZE);

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

	pdu->protocolversion = 0;
	pdu->msg.attributetype = MSRP_ATTRIBUTE_TYPE_LISTENER_VECTOR;
	pdu->msg.attributelen  = MSRP_ATTRIBUTE_LEN_LISTENER_VECTOR;
	pdu->msg.attributelistlen = avb_change_to_big_endian_u16(sizeof(struct listnervectorattribute));

	if(state != MSRP_DECLARATION_STATE_UNKNOWN) {
		pdu->msg.attibutelist.hdr.numberofvalues = avb_change_to_big_endian_u16(1);
		pdu->msg.attibutelist.val.streamid[0] = msrp->streamid[0];
		pdu->msg.attibutelist.val.streamid[1] = msrp->streamid[1];
		pdu->msg.attibutelist.val.streamid[2] = msrp->streamid[2];
		pdu->msg.attibutelist.val.streamid[3] = msrp->streamid[3];
		pdu->msg.attibutelist.val.streamid[4] = msrp->streamid[4];
		pdu->msg.attibutelist.val.streamid[5] = msrp->streamid[5];
		pdu->msg.attibutelist.val.streamid[6] = msrp->streamid[6];
		pdu->msg.attibutelist.val.streamid[7] = msrp->streamid[7];

		pdu->msg.attibutelist.vector[0] = MSRP_THREE_PACK(((join == true)?(MSRP_ATTRIBUTE_EVENT_JOININ):(MSRP_ATTRIBUTE_EVENT_LEAVE)), 0, 0);
		pdu->msg.attibutelist.vector[1] = MSRP_FOUR_PACK(state, 0, 0, 0);
	} else {
		pdu->msg.attibutelist.vector[0] = MSRP_THREE_PACK(MSRP_ATTRIBUTE_EVENT_JOINMT, 0, 0);
		pdu->msg.attibutelist.vector[1] = MSRP_FOUR_PACK(MSRP_DECLARATION_STATE_READY, 0, 0, 0);
	}

	pdu->msg.endmarker = 0;
	pdu->endmarker = 0;

	txSize = sizeof(struct ethhdr) + sizeof(struct listnermsrpdu);

	msrp->sd.txiov.iov_base = msrp->sd.txBuf;
	msrp->sd.txiov.iov_len = txSize;

	vec.iov_base = msrp->sd.txiov.iov_base;
	vec.iov_len = msrp->sd.txiov.iov_len;

	iov_iter_init(&msrp->sd.txMsgHdr.msg_iter, WRITE, &msrp->sd.txiov, 1, txSize);

	if ((err = kernel_sendmsg(msrp->sd.sock, &msrp->sd.txMsgHdr, &vec, 1, txSize)) <= 0) {
		avb_log(AVB_KERN_WARN, KERN_WARNING "avb_msrp_listenerdeclarations Socket transmission fails %d \n", err);
		return;
	}
}

static void avb_msrp_evaluateTalkerAdvertisement(struct msrp* msrp)
{
	struct talkermsrpdu *tpdu = (struct talkermsrpdu*)&msrp->sd.rxBuf[sizeof(struct ethhdr)];

	int leaveAll = avb_change_to_big_endian_u16(tpdu->msg.attibutelist.hdr.numberofvalues) & 0x2000;
	int evt = MSRP_THREE_PACK_GET_A(tpdu->msg.attibutelist.vector[0]);

	if(leaveAll != 0) {
		msrp->txState = MSRP_DECLARATION_STATE_UNKNOWN;
	} else {
		if(tpdu->msg.attributetype == MSRP_ATTRIBUTE_TYPE_TALKER_ADVERTISE_VECTOR) {
			if((evt == MSRP_ATTRIBUTE_EVENT_JOINMT) || (evt == MSRP_ATTRIBUTE_EVENT_MT) || (evt == MSRP_ATTRIBUTE_EVENT_LEAVE)) {
				msrp->rxState = MSRP_DECLARATION_STATE_NONE;
				memset(&msrp->streamid[0], 0, 8);
			} else {
				msrp->rxState = MSRP_DECLARATION_STATE_READY;
			}
		} else {
			msrp->rxState = MSRP_DECLARATION_STATE_ASKING_FAILED;	
		}

		memcpy(&msrp->streamid[0], &tpdu->msg.attibutelist.val.streamid[0], 8);
	}
}

static void avb_msrp_evaluateListnerAdvertisement(struct msrp* msrp)
{
	struct listnermsrpdu *pdu = (struct listnermsrpdu*)&msrp->sd.rxBuf[sizeof(struct ethhdr)];

	int leaveAll = avb_change_to_big_endian_u16(pdu->msg.attibutelist.hdr.numberofvalues) & 0x2000;
	int evt = MSRP_THREE_PACK_GET_A(pdu->msg.attibutelist.vector[0]);

	if(leaveAll != 0) {
		msrp->rxState = MSRP_DECLARATION_STATE_UNKNOWN;
	} else {
		if((evt == MSRP_ATTRIBUTE_EVENT_JOINMT) || (evt == MSRP_ATTRIBUTE_EVENT_MT) || (evt == MSRP_ATTRIBUTE_EVENT_LEAVE)) {
			msrp->txState = MSRP_DECLARATION_STATE_NONE;
		} else {
			if((pdu->msg.attibutelist.val.streamid[0] == msrp->sd.srcmac[0]) &&
			   (pdu->msg.attibutelist.val.streamid[1] == msrp->sd.srcmac[1]) && 
			   (pdu->msg.attibutelist.val.streamid[2] == msrp->sd.srcmac[2]) && 
			   (pdu->msg.attibutelist.val.streamid[3] == msrp->sd.srcmac[3]) && 
			   (pdu->msg.attibutelist.val.streamid[4] == msrp->sd.srcmac[4]) && 
			   (pdu->msg.attibutelist.val.streamid[5] == msrp->sd.srcmac[5]) &&
			   (pdu->msg.attibutelist.val.streamid[6] == 0) && 
			   (pdu->msg.attibutelist.val.streamid[7] == 1)) {
				msrp->txState = MSRP_DECLARATION_STATE_READY;
			}
		}
	}
}

static int avb_msrp_listen(struct msrp* msrp)
{
	int err = 0;
	mm_segment_t oldfs;
	struct listnermsrpdu *tpdu = (struct listnermsrpdu*)&msrp->sd.rxBuf[sizeof(struct ethhdr)];
	struct kvec vec;

	memset(msrp->sd.rxBuf, 0, AVB_MAX_ETH_FRAME_SIZE);
	msrp->sd.rxiov.iov_base = msrp->sd.rxBuf;
	msrp->sd.rxiov.iov_len = AVB_MAX_ETH_FRAME_SIZE;

	vec.iov_base = msrp->sd.rxiov.iov_base;
	vec.iov_len = msrp->sd.rxiov.iov_len;

	iov_iter_init(&msrp->sd.rxMsgHdr.msg_iter, READ, &msrp->sd.rxiov, 1, AVB_MAX_ETH_FRAME_SIZE);

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = kernel_recvmsg(msrp->sd.sock, &msrp->sd.rxMsgHdr, &vec, 1 , msrp->sd.rxiov.iov_len, 0);
	set_fs(oldfs);
	
	if (err <= 0) {
		if(err != -11)
			avb_log(AVB_KERN_WARN, KERN_WARNING "avb_msrp_listen Socket reception res %d \n", err);
	} else {
		if(tpdu->protocolversion != 0) {
			avb_log(AVB_KERN_WARN, KERN_WARNING "avb_msrp_listen unknown protocolversion %d \n", tpdu->protocolversion);
		} else {
			if((tpdu->msg.attributetype == MSRP_ATTRIBUTE_TYPE_TALKER_ADVERTISE_VECTOR) ||
			   (tpdu->msg.attributetype == MSRP_ATTRIBUTE_TYPE_TALKER_FAILED_VECTOR)) {
				avb_msrp_evaluateTalkerAdvertisement(msrp);
			} else if(tpdu->msg.attributetype == MSRP_ATTRIBUTE_TYPE_LISTENER_VECTOR) {
				avb_msrp_evaluateListnerAdvertisement(msrp);
			} else if(tpdu->msg.attributetype == MSRP_ATTRIBUTE_TYPE_DOMAIN_VECTOR) {
			} else {
				avb_log(AVB_KERN_WARN, KERN_WARNING "avb_msrp_listen unknown attribute type %d \n", tpdu->msg.attributetype);
			}

			avb_log(AVB_KERN_NOT, KERN_NOTICE "avb_msrp_listen: rxType: %d, rxState: %d, txState: %d", tpdu->msg.attributetype,
				msrp->rxState, msrp->txState);		
		}
	}

	return err;
}

static void avbWqFn(struct work_struct *work)
{
	int err = 0;
	int rxCount = 0;
	int fillsize = 0;
	int rxFrames = -1;
	int rxLoopCount = 0;
	struct workdata* wd = (struct workdata*)work;

	if(wd->delayedWorkId == AVB_DELAY_WORK_MSRP) {

		if(wd->dw.msrp->initialized == false)
			wd->dw.msrp->initialized = avb_msrp_init(wd->dw.msrp);

		if(wd->dw.msrp->initialized == false) {
			queue_delayed_work(avbdevice.wq, (struct delayed_work*)avbdevice.msrpwd, 10000);
		} else {
			if(wd->dw.msrp->started == true) {
				do {
					err = avb_msrp_listen(wd->dw.msrp);
					rxCount += ((err > 0)?(1):(0));
				} while(err > 0);

				if(rxCount == 0) {
					if((wd->dw.msrp->txState == MSRP_DECLARATION_STATE_NONE) || (wd->dw.msrp->txState == MSRP_DECLARATION_STATE_READY)) {
						avb_msrp_talkerdeclarations(wd->dw.msrp, true, wd->dw.msrp->txState);
					}
				} else {
					if((wd->dw.msrp->txState == MSRP_DECLARATION_STATE_NONE) || (wd->dw.msrp->txState == MSRP_DECLARATION_STATE_UNKNOWN)) {
						avb_msrp_talkerdeclarations(wd->dw.msrp, true, wd->dw.msrp->txState);
					}

					if((wd->dw.msrp->rxState == MSRP_DECLARATION_STATE_NONE) || (wd->dw.msrp->rxState == MSRP_DECLARATION_STATE_READY)) {
						avb_msrp_listenerdeclarations(wd->dw.msrp, true, wd->dw.msrp->rxState);
					}

					wd->dw.msrp->txState = ((wd->dw.msrp->txState == MSRP_DECLARATION_STATE_UNKNOWN)?(MSRP_DECLARATION_STATE_NONE):(wd->dw.msrp->txState));
					wd->dw.msrp->rxState = ((wd->dw.msrp->rxState == MSRP_DECLARATION_STATE_UNKNOWN)?(MSRP_DECLARATION_STATE_NONE):(wd->dw.msrp->rxState));
				}
			}

			avb_msrp_domaindeclarations(wd->dw.msrp);

			queue_delayed_work(avbdevice.wq, (struct delayed_work*)avbdevice.msrpwd, msecs_to_jiffies(2000));
		}
	} else if(wd->delayedWorkId == AVB_DELAY_WORK_AVDECC) {

		if(wd->dw.avdecc->initialized == false)
			wd->dw.avdecc->initialized = avb_avdecc_init(wd->dw.avdecc);

		if(wd->dw.avdecc->initialized == false) {
			queue_delayed_work(avbdevice.wq, (struct delayed_work*)avbdevice.avdeccwd, 10000);
		} else {
			if(wd->dw.avdecc->lastADPAdvJiffy == 0)
				avb_adp_discover(wd->dw.avdecc);
			if(((jiffies - wd->dw.avdecc->lastADPAdvJiffy) >= 2000) || (wd->dw.avdecc->lastADPAdvJiffy == 0)) {		
				avb_adp_advertise(wd->dw.avdecc);
				avb_maap_announce(wd->dw.avdecc);
				wd->dw.avdecc->lastADPAdvJiffy = jiffies;
			}
			avb_avdecc_listen_and_respond(wd->dw.avdecc);
			queue_delayed_work(avbdevice.wq, (struct delayed_work*)avbdevice.avdeccwd, 1);
		}
	} else if(wd->delayedWorkId == AVB_DELAY_WORK_AVTP) {

		memcpy(&avbdevice.card, wd->dw.card, sizeof(struct avbcard));

		do {
			rxFrames = avb_avtp_listen(&avbdevice.card);

			if(rxFrames > 0) {
				avbdevice.card.rx.hwIdx += rxFrames;
				avbdevice.card.rx.hwnwIdx += rxFrames;
				avbdevice.card.rx.hwIdx %= avbdevice.card.rx.framecount;

				if (avbdevice.card.rx.hwIdx < avbdevice.card.rx.prevHwIdx)
				        fillsize = avbdevice.card.rx.framecount + avbdevice.card.rx.prevHwIdx - avbdevice.card.rx.hwIdx;
				else
				        fillsize = avbdevice.card.rx.hwIdx - avbdevice.card.rx.prevHwIdx;

				avbdevice.card.rx.prevHwIdx = avbdevice.card.rx.hwIdx;
				avbdevice.card.rx.fillsize += fillsize;

				avb_log(AVB_KERN_INFO, KERN_INFO "avbWqFn: AVTP-%lu @ %lu rxFrms:%d hwIdx:%lu filSz: %lu",
					rxLoopCount++, jiffies, rxFrames, avbdevice.card.rx.hwIdx, avbdevice.card.rx.fillsize);
		
				if(avbdevice.card.rx.fillsize >= avbdevice.card.rx.periodsize) {
					avbdevice.card.rx.fillsize %= avbdevice.card.rx.periodsize;
					snd_pcm_period_elapsed(avbdevice.card.rx.substream);
				}
			} else {
				break;
			}
		

			memcpy(wd->dw.card, &avbdevice.card, sizeof(struct avbcard));

		} while(rxFrames > 0);

		if(avbdevice.avtpwd != NULL) {
			queue_delayed_work(avbdevice.wq, (struct delayed_work*)avbdevice.avtpwd, 1);
		}
	} else {
		avb_log(AVB_KERN_INFO, KERN_INFO "avbWqFn: Unknown: %d", wd->delayedWorkId);
	}
}


static int avb_pcm_new(struct avbcard *avbc, int device, int substreams)
{
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(avbc->card, "AVB PCM", device,
			  substreams, substreams, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &avb_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &avb_capture_ops);

	pcm->private_data = avbc;
	pcm->info_flags = 0;
	strcpy(pcm->name, "AVB PCM");

	avbc->pcm[device] = pcm;

	return 0;
}

static int avb_hwdep_open(struct snd_hwdep * hw, struct file *file)
{
	avb_log(AVB_KERN_NOT, KERN_NOTICE "avb_hwdep_open");

	return 0;
}

static int avb_hwdep_ioctl(struct snd_hwdep * hw, struct file *file, unsigned int cmd, unsigned long arg)
{
	int res = 0;

	if(cmd == 0) {
		avb_log(AVB_KERN_INFO, KERN_INFO "avb_hwdep_ioctl set ts: %ld @ idx: %d", arg, avbdevice.txIdx);
		avbdevice.txts[avbdevice.txIdx] = arg;
		avbdevice.txIdx++;
		avbdevice.txIdx %= AVB_MAX_TS_SLOTS;
	} else {
		res = copy_to_user((void*)arg, &avbdevice.rxts[avbdevice.rxIdx], sizeof(unsigned long));
		avb_log(AVB_KERN_INFO, KERN_INFO "avb_hwdep_ioctl get ts: %d @ %d, res: %d", avbdevice.rxts[avbdevice.rxIdx], avbdevice.rxIdx, res);
		avbdevice.rxIdx++;
		avbdevice.rxIdx %= AVB_MAX_TS_SLOTS;
	}

	return 0;
}

static int avb_hwdep_release(struct snd_hwdep * hw, struct file *file)
{
	avb_log(AVB_KERN_NOT, KERN_NOTICE "avb_hwdep_release");

	return 0;
}

#ifdef CONFIG_PM_SLEEP

static int avb_suspend(struct device *pdev)
{
	avb_log(AVB_KERN_INFO, KERN_INFO "avb_suspend");
	return 0;
}
	
static int avb_resume(struct device *pdev)
{
	avb_log(AVB_KERN_INFO, KERN_INFO "avb_resume");
	return 0;
}

#endif

static int avb_probe(struct platform_device *devptr)
{
	struct snd_card *card;
	struct avbcard *avbcard;
	int dev = devptr->id;
	int err;

	avb_log(AVB_KERN_NOT, KERN_NOTICE "avb_probe");

	err = snd_card_new(&devptr->dev, index[dev], id[dev], THIS_MODULE,
			   sizeof(struct avbcard), &card);

	if (err < 0) {
		avb_log(AVB_KERN_ERR, KERN_ERR "avb_probe card new err: %d", err);
		return err;
	}

	avbcard = card->private_data;
	avbcard->card = card;

	err = avb_pcm_new(avbcard, 0, pcm_substreams[dev]);
	if (err < 0) {
		avb_log(AVB_KERN_ERR, KERN_ERR "avb_probe card pcm new err: %d", err);
		goto __nodev;
	}

	err = snd_hwdep_new(card, "avbhw", 0, &avbdevice.hwdep);
	if(err < 0) {
		avb_log(AVB_KERN_ERR, KERN_ERR "avb_probe card hwdep new err: %d", err);
		goto __nodev;
	}
	
	avbdevice.hwdep->ops.open    = avb_hwdep_open;
	avbdevice.hwdep->ops.ioctl   = avb_hwdep_ioctl;
	avbdevice.hwdep->ops.release = avb_hwdep_release;

	strcpy(card->driver, "avb");
	strcpy(card->shortname, "avb");
	sprintf(card->longname, "avb %i", dev + 1);
	err = snd_card_register(card);
	if (!err) {
		platform_set_drvdata(devptr, card);
	
		avbcard->sd.type = ETH_P_TSN;
		avbcard->sd.destmac[0] = 0x01;
		avbcard->sd.destmac[1] = 0x80;
		avbcard->sd.destmac[2] = 0xC2;
		avbcard->sd.destmac[3] = 0x00;
		avbcard->sd.destmac[4] = 0x00;
		avbcard->sd.destmac[5] = 0x0E;

		if(!avb_socket_init(&avbcard->sd, 100)) {
			avb_log(AVB_KERN_ERR, KERN_ERR "avb_probe socket init failed");
			err = -1;
			goto __nodev;	
		}

		return 0;
	}

	avb_log(AVB_KERN_INFO, KERN_INFO "avb_probe card reg err: %d", err);

__nodev:
	snd_card_free(card);

	return err;
}

static int avb_remove(struct platform_device *devptr)
{
	avb_log(AVB_KERN_NOT, KERN_NOTICE "avb_remove");
	snd_card_free(platform_get_drvdata(devptr));
	return 0;
}

static void avb_remove_all(void) {
	int i = 0;

	avb_log(AVB_KERN_NOT, KERN_NOTICE "avb_remove_all");

	for(i=0; i < numcards; i++)
		platform_device_unregister(avbdevices[i]);
}											

static int __init alsa_avb_init(void)
{
	int i, err;
	struct platform_device *dev;
	avb_log(AVB_KERN_NOT, KERN_NOTICE "alsa_avb_init");

	err = platform_driver_register(&avb_driver);
	if (err < 0) {
		avb_log(AVB_KERN_ERR, KERN_ERR "alsa_avb_init reg err %d", err);
		return err;
	}

	for(i=0; i < SND_AVB_NUM_CARDS; i++) {
		if(!enable[i])
			continue;

		dev = platform_device_register_simple(SND_AVB_DRIVER, i, NULL, 0);

		if (IS_ERR(dev)) {		
			avb_log(AVB_KERN_ERR, KERN_ERR "alsa_avb_init regsimple err");
			continue;
		}

		if (!platform_get_drvdata(dev)) {
			avb_log(AVB_KERN_ERR, KERN_ERR "alsa_avb_init getdrvdata err");
			platform_device_unregister(dev);
			continue;
		}

		avbdevices[i] = dev;
		numcards++;
	}

	if(!numcards) {
		avb_remove_all();
	} else {
		memset(&avbdevice, 0, sizeof(struct avbdevice));

		avbdevice.wq = create_workqueue(AVB_WQ);
		if(avbdevice.wq == NULL) {
			avb_log(AVB_KERN_ERR, KERN_ERR "alsa_avb_init workqueue creation failed");
			return -1;
		}

		avbdevice.msrpwd = avb_init_and_queue_work(AVB_DELAY_WORK_MSRP, (void*)&avbdevice.msrp, 100);
		avbdevice.avdeccwd = avb_init_and_queue_work(AVB_DELAY_WORK_AVDECC, (void*)&avbdevice.avdecc, 100);

		avb_log(AVB_KERN_NOT, KERN_NOTICE "alsa_avb_init done err: %d, numcards: %d", err, numcards);	
	}

	return 0;
}

static void __exit alsa_avb_exit(void)
{
	avb_log(AVB_KERN_NOT, KERN_NOTICE "alsa_avb_exit");
	
	if(avbdevice.msrpwd != NULL) {
		cancel_delayed_work((struct delayed_work*)avbdevice.msrpwd);
		kfree(avbdevice.msrpwd);
		avbdevice.msrpwd = NULL;
	}

	if(avbdevice.avdeccwd != NULL) {
		cancel_delayed_work((struct delayed_work*)avbdevice.avdeccwd);
		kfree(avbdevice.avdeccwd);
		avbdevice.avdeccwd = NULL;
	}

	if(avbdevice.wq != NULL) {
		flush_workqueue(avbdevice.wq);
		destroy_workqueue(avbdevice.wq);
	}

	avb_remove_all();

	platform_driver_unregister(&avb_driver);
	
	avb_log(AVB_KERN_NOT, KERN_NOTICE "alsa_avb_exit done");
}

module_init(alsa_avb_init)
module_exit(alsa_avb_exit)


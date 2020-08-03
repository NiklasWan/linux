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

#include "avb_base.h"

MODULE_AUTHOR("Indumathi Duraipandian <indu9086@gmail.com>");
MODULE_DESCRIPTION("AVB soundcard");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{ALSA,AVB soundcard}}");

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

static int avb_playback_open(struct snd_pcm_substream *substream);
static int avb_playback_close(struct snd_pcm_substream *substream);
static int avb_playback_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params);
static int avb_playback_hw_free(struct snd_pcm_substream *substream);
static int avb_playback_prepare(struct snd_pcm_substream *substream);
static int avb_playback_trigger(struct snd_pcm_substream *substream, int cmd);
static snd_pcm_uframes_t avb_playback_pointer(struct snd_pcm_substream *substream);
static int avb_playback_copy(struct snd_pcm_substream *substream,
                       int channel, unsigned long pos,
                       void __user *src,
                       unsigned long bytes);



static int avb_capture_open(struct snd_pcm_substream *substream);
static int avb_capture_close(struct snd_pcm_substream *substream);
static int avb_capture_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params);
static int avb_capture_hw_free(struct snd_pcm_substream *substream);
static int avb_capture_prepare(struct snd_pcm_substream *substream);
static int avb_capture_trigger(struct snd_pcm_substream *substream, int cmd);
static snd_pcm_uframes_t avb_capture_pointer(struct snd_pcm_substream *substream);
static int avb_capture_copy(struct snd_pcm_substream *substream,
                       int channel, unsigned long pos,
                       void __user *dst,
                       unsigned long bytes);

static int avb_pcm_new(struct avbcard *avbc, int device, int substreams);
void avb_wq_fn(struct work_struct *work);
static int avb_probe(struct platform_device *devptr);
static int avb_remove(struct platform_device *devptr);
static void avb_remove_all(void);
static int __init alsa_avb_init(void);
static void __exit alsa_avb_exit(void);

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

static int avb_playback_open(struct snd_pcm_substream *substream)
{
	printk(KERN_NOTICE "avb_playback_open");

        substream->runtime->hw = avb_playback_hw;

	return 0;
}

static int avb_playback_close(struct snd_pcm_substream *substream)
{
	printk(KERN_NOTICE "avb_playback_close");

	return 0;
}


struct workdata* avb_init_and_queue_work(int work_id, void* wdata, int delay)
{
	struct workdata* wd;

	wd = (struct workdata*) kmalloc(sizeof(struct workdata), GFP_KERNEL);

	if(wd == NULL) {
		printk(KERN_ERR "avb_init_and_queue_work workdata allocation failed for work %d", work_id);
		return NULL;
	}

	wd->dw.data = wdata;
	wd->delayed_work_id = work_id;
	INIT_DELAYED_WORK((struct delayed_work*)wd, avb_wq_fn);
			
	queue_delayed_work(avbdevice.wq, (struct delayed_work*)wd, delay);

	return wd;
}

int avb_avtp_listen(struct avbcard* avbcard)
{
	int err = 0;
	u8* src_buf;
	u8* dest_buf;
	int rx_off = 0;
	int rx_size = 0;
	int nrx_size = 0;
	int avai_size = 0;
	int rx_frames = 0;
	int nrx_frames = 0;
	int next_seq_no = 0;
	int skipped_packets = 0;
	mm_segment_t oldfs;
	snd_pcm_uframes_t hw_idx = 0;
	struct avt_pdu_aaf_pcm_hdr* hdr = (struct avt_pdu_aaf_pcm_hdr*)&avbcard->sd.rx_buf[sizeof(struct ethhdr)];
	struct kvec vec;

	memset(avbcard->sd.rx_buf, 0, AVB_MAX_ETH_FRAME_SIZE);
	avbcard->sd.rxiov.iov_base = avbcard->sd.rx_buf;
	avbcard->sd.rxiov.iov_len = AVB_MAX_ETH_FRAME_SIZE;
	vec.iov_base = avbcard->sd.rxiov.iov_base;
	vec.iov_len = avbcard->sd.rxiov.iov_len;
	iov_iter_init(&avbcard->sd.rx_msg_hdr.msg_iter, READ, &avbcard->sd.rxiov, 1, AVB_MAX_ETH_FRAME_SIZE);

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = kernel_recvmsg(avbcard->sd.sock, &avbcard->sd.rx_msg_hdr, &vec, 1, avbcard->sd.rxiov.iov_len, 0);
	set_fs(oldfs);

	if (err > 0) {
		avbcard->rx.socket_count++;

		rx_off   = sizeof(struct ethhdr) + sizeof(struct avt_pdu_aaf_pcm_hdr);
		src_buf  = (u8*)&avbcard->sd.rx_buf[rx_off];

		rx_size = hdr->h.f.stream_data_len;
		rx_frames = (rx_size / avbcard->rx.frame_size);

		avbdevice.rx_ts[((avbcard->rx.hwnw_idx / avbcard->rx.period_size) % AVB_MAX_TS_SLOTS)] = hdr->h.f.avtp_ts;

		next_seq_no = (avbcard->rx.seq_no + 1) % 256;

		if(next_seq_no != hdr->h.f.seq_no) {
			printk(KERN_ERR "avb_listen missing frames from %d to %d \n",
				avbcard->rx.seq_no, hdr->h.f.seq_no);

			skipped_packets = ((hdr->h.f.seq_no >= avbcard->rx.seq_no)? \
						(hdr->h.f.seq_no - avbcard->rx.seq_no): \
						((hdr->h.f.seq_no + 255) - avbcard->rx.seq_no));
			nrx_frames = (skipped_packets * rx_frames);
			nrx_size = nrx_frames * avbcard->rx.frame_size;

			printk(KERN_INFO "avb_listen idx: %ld nrsz: %d, nrf: %d \n",
				avbcard->rx.hw_idx, nrx_size, nrx_frames);

			avai_size = ((avbcard->rx.frame_count - avbcard->rx.hw_idx) * avbcard->rx.frame_size);
			avai_size = ((avai_size < nrx_size)?(avai_size):(nrx_size));
			dest_buf  = (u8*)&avbcard->rx.tmp_buf[avbcard->rx.hw_idx * avbcard->rx.frame_size];
			memset(dest_buf, 0, avai_size);

			if(avai_size < nrx_size) {
				dest_buf  = (u8*)&avbcard->rx.tmp_buf[0];
				memset(dest_buf, 0, (nrx_size - avai_size));
			}

			hw_idx = ((avbcard->rx.hw_idx + nrx_frames) % (avbcard->rx.frame_count));
			rx_frames = rx_frames + nrx_frames;
		} else {
			hw_idx = avbcard->rx.hw_idx;
		}

		printk(KERN_INFO "avb_listen (%d) seq: %d, idx: %ld, sz: %d, ts: %u, rf: %d \n",
			avbcard->rx.socket_count, hdr->h.f.seq_no, hw_idx, rx_size, hdr->h.f.avtp_ts, rx_frames);

		avbcard->rx.seq_no = hdr->h.f.seq_no;

		avai_size = ((avbcard->rx.frame_count - hw_idx) * avbcard->rx.frame_size);
		avai_size = ((avai_size < rx_size)?(avai_size):(rx_size));
		dest_buf  = (u8*)&avbcard->rx.tmp_buf[hw_idx * avbcard->rx.frame_size];
		memcpy(dest_buf, src_buf, avai_size);

		if(avai_size < rx_size) {
			dest_buf  = (u8*)&avbcard->rx.tmp_buf[0];
			memcpy(dest_buf, &src_buf[avai_size], (rx_size - avai_size));
		}
	} else {
		if(err != -11)
			printk(KERN_WARNING "avb_avtp_listen Socket reception fails %d \n", err);
		return 0;
	}

	return rx_frames;
}

void avb_wq_fn(struct work_struct *work)
{
	int err = 0;
	int rxCount = 0;
	int fill_size = 0;
	int rx_frames = -1;
	int rx_loop_count = 0;
	struct workdata* wd = (struct workdata*)work;

	if(wd->delayed_work_id == AVB_DELAY_WORK_MSRP) {

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
					if((wd->dw.msrp->tx_state == MSRP_DECLARATION_STATE_NONE) || (wd->dw.msrp->tx_state == MSRP_DECLARATION_STATE_READY)) {
						avb_msrp_talkerdeclarations(wd->dw.msrp, true, wd->dw.msrp->tx_state);
					}
				} else {
					if((wd->dw.msrp->tx_state == MSRP_DECLARATION_STATE_NONE) || (wd->dw.msrp->tx_state == MSRP_DECLARATION_STATE_UNKNOWN)) {
						avb_msrp_talkerdeclarations(wd->dw.msrp, true, wd->dw.msrp->tx_state);
					}

					if((wd->dw.msrp->rx_state == MSRP_DECLARATION_STATE_NONE) || (wd->dw.msrp->rx_state == MSRP_DECLARATION_STATE_READY)) {
						avb_msrp_listenerdeclarations(wd->dw.msrp, true, wd->dw.msrp->rx_state);
					}

					wd->dw.msrp->tx_state = ((wd->dw.msrp->tx_state == MSRP_DECLARATION_STATE_UNKNOWN)?(MSRP_DECLARATION_STATE_NONE):(wd->dw.msrp->tx_state));
					wd->dw.msrp->rx_state = ((wd->dw.msrp->rx_state == MSRP_DECLARATION_STATE_UNKNOWN)?(MSRP_DECLARATION_STATE_NONE):(wd->dw.msrp->rx_state));
				}
			}

			avb_msrp_domaindeclarations(wd->dw.msrp);

			queue_delayed_work(avbdevice.wq, (struct delayed_work*)avbdevice.msrpwd, msecs_to_jiffies(2000));
		}
	} else if(wd->delayed_work_id == AVB_DELAY_WORK_AVDECC) {

		if(wd->dw.avdecc->initialized == false)
			wd->dw.avdecc->initialized = avb_avdecc_init(wd->dw.avdecc);

		if(wd->dw.avdecc->initialized == false) {
			queue_delayed_work(avbdevice.wq, (struct delayed_work*)avbdevice.avdeccwd, 10000);
		} else {
			if(wd->dw.avdecc->last_ADP_adv_jiffy == 0)
				avb_adp_discover(wd->dw.avdecc);
			if(((jiffies - wd->dw.avdecc->last_ADP_adv_jiffy) >= 2000) || (wd->dw.avdecc->last_ADP_adv_jiffy == 0)) {		
				avb_adp_advertise(wd->dw.avdecc);
				avb_maap_announce(wd->dw.avdecc);
				wd->dw.avdecc->last_ADP_adv_jiffy = jiffies;
			}
			avb_avdecc_listen_and_respond(wd->dw.avdecc, &avbdevice.msrp);
			queue_delayed_work(avbdevice.wq, (struct delayed_work*)avbdevice.avdeccwd, 1);
		}
	} else if(wd->delayed_work_id == AVB_DELAY_WORK_AVTP) {

		memcpy(&avbdevice.card, wd->dw.card, sizeof(struct avbcard));

		do {
			rx_frames = avb_avtp_listen(&avbdevice.card);

			if(rx_frames > 0) {
				avbdevice.card.rx.hw_idx += rx_frames;
				avbdevice.card.rx.hwnw_idx += rx_frames;
				avbdevice.card.rx.hw_idx %= avbdevice.card.rx.frame_count;

				if (avbdevice.card.rx.hw_idx < avbdevice.card.rx.prev_hw_idx)
				        fill_size = avbdevice.card.rx.frame_count + avbdevice.card.rx.prev_hw_idx - avbdevice.card.rx.hw_idx;
				else
				        fill_size = avbdevice.card.rx.hw_idx - avbdevice.card.rx.prev_hw_idx;

				avbdevice.card.rx.prev_hw_idx = avbdevice.card.rx.hw_idx;
				avbdevice.card.rx.fill_size += fill_size;

				printk(KERN_INFO "avb_wq_fn: AVTP-%d @ %lu rxFrms:%d hw_idx:%lu filSz: %lu",
					rx_loop_count++, jiffies, rx_frames, avbdevice.card.rx.hw_idx, avbdevice.card.rx.fill_size);
		
				if(avbdevice.card.rx.fill_size >= avbdevice.card.rx.period_size) {
					avbdevice.card.rx.fill_size %= avbdevice.card.rx.period_size;
					snd_pcm_period_elapsed(avbdevice.card.rx.substream);
				}
			} else {
				break;
			}
		

			memcpy(wd->dw.card, &avbdevice.card, sizeof(struct avbcard));

		} while(rx_frames > 0);

		if(avbdevice.avtpwd != NULL) {
			queue_delayed_work(avbdevice.wq, (struct delayed_work*)avbdevice.avtpwd, 1);
		}
	} else {
		printk(KERN_INFO "avb_wq_fn: Unknown: %d", wd->delayed_work_id);
	}
}

enum hrtimer_restart avb_avtp_timer(struct hrtimer* t)
{
	ktime_t kt;
	int i = 0;
	int err = 0;
	int tx_size = 0;
	snd_pcm_uframes_t bytes_avai = 0;
	snd_pcm_uframes_t bytes_to_copy  = 0;
	snd_pcm_uframes_t frames_to_copy = 0;
	snd_pcm_uframes_t avtp_frames_per_packet = 0;
	snd_pcm_uframes_t avtp_max_frames_per_packet = 0;
	enum hrtimer_restart hr_res = HRTIMER_NORESTART;
	struct kvec vec;


	struct avbcard *avbcard = ((struct avbhrtimer *)t)->card;

	struct avt_pdu_aaf_pcm_hdr* hdr = (struct avt_pdu_aaf_pcm_hdr*)&avbcard->sd.tx_buf[sizeof(struct ethhdr)];

	avtp_max_frames_per_packet = ((ETH_DATA_LEN - sizeof(struct avt_pdu_aaf_pcm_hdr)) / avbcard->tx.frame_size);

	avbcard->tx.accum_frame_count += avtp_max_frames_per_packet;
	avtp_frames_per_packet = avtp_max_frames_per_packet;
	kt = ktime_set(0, avbcard->tx.timer_val);
	printk(KERN_INFO "avb_avtp_timer mfppk: %lu, fppk: %lu, frSz: %lu, sr: %d, time: %lu",
		avtp_max_frames_per_packet, avtp_frames_per_packet, avbcard->tx.frame_size, avbcard->tx.sr, avbcard->tx.timer_val);


	while(((avbcard->tx.accum_frame_count >= avtp_frames_per_packet) ||
	       (avbcard->tx.pending_tx_frames <= avtp_frames_per_packet)) &&
	      ((avbcard->tx.pending_tx_frames > 0) && (i < 1))) { 

		i++; /* Just as a failsafe to quit loop */
		avbcard->tx.seq_no++;
		hdr->h.f.seq_no = avbcard->tx.seq_no;

		tx_size = sizeof(struct ethhdr) + sizeof(struct avt_pdu_aaf_pcm_hdr);
		frames_to_copy = ((avbcard->tx.pending_tx_frames > avtp_frames_per_packet)?(avtp_frames_per_packet):(avbcard->tx.pending_tx_frames));
		bytes_to_copy  = (frames_to_copy * avbcard->tx.frame_size);

		bytes_avai = ((avbcard->tx.frame_count - avbcard->tx.hw_idx) * avbcard->tx.frame_size);
		bytes_avai = ((bytes_avai >= bytes_to_copy)?(bytes_to_copy):(bytes_avai));

		memcpy(&avbcard->sd.tx_buf[tx_size], &avbcard->tx.tmp_buf[(avbcard->tx.hw_idx * avbcard->tx.frame_size)], bytes_avai);

		if(bytes_avai < bytes_to_copy) {
			memcpy(&avbcard->sd.tx_buf[tx_size+bytes_avai], &avbcard->tx.tmp_buf[0], (bytes_to_copy - bytes_avai));
		}

		hdr->h.f.avtp_ts = avbdevice.tx_ts[((avbcard->tx.hwnw_idx / avbcard->tx.period_size) % AVB_MAX_TS_SLOTS)];
		hdr->h.f.stream_data_len = bytes_to_copy;
		tx_size += bytes_to_copy;

		avbcard->sd.txiov.iov_base = avbcard->sd.tx_buf;
		avbcard->sd.txiov.iov_len  = tx_size;

		vec.iov_base = avbcard->sd.txiov.iov_base;
		vec.iov_len = avbcard->sd.txiov.iov_len;

		iov_iter_init(&avbcard->sd.tx_msg_hdr.msg_iter, WRITE, &avbcard->sd.txiov, 1, tx_size);

		if ((err = kernel_sendmsg(avbcard->sd.sock, &avbcard->sd.tx_msg_hdr, &vec, 1, tx_size)) <= 0) {
			printk(KERN_WARNING "avb_avtp_timer Socket transmission fails %d \n", err);
			goto end;
		}

		avbcard->tx.accum_frame_count = ((avbcard->tx.accum_frame_count > frames_to_copy)?(avbcard->tx.accum_frame_count - frames_to_copy):(0));

		avbcard->tx.hw_idx += frames_to_copy;
		avbcard->tx.hwnw_idx += frames_to_copy;
		avbcard->tx.fill_size += frames_to_copy;
		avbcard->tx.hw_idx = ((avbcard->tx.hw_idx < avbcard->tx.frame_count)?(avbcard->tx.hw_idx):(avbcard->tx.hw_idx % avbcard->tx.frame_count));
		avbcard->tx.pending_tx_frames -= frames_to_copy;

		printk(KERN_INFO "avb_avtp_timer seq_no:%d, hw_idx: %lu, afrCt: %lu, penFrs:%lu, filSz:%lu",
			hdr->h.f.seq_no, avbcard->tx.hw_idx, avbcard->tx.accum_frame_count, avbcard->tx.pending_tx_frames, avbcard->tx.fill_size);

		if(avbcard->tx.fill_size >= avbcard->tx.period_size) {
			avbcard->tx.fill_size %= avbcard->tx.period_size;
			snd_pcm_period_elapsed(avbcard->tx.substream);
		}
	}

end:
	if(avbcard->tx.st == 1) {
		hrtimer_forward_now(t, kt);
		hr_res = HRTIMER_RESTART;	
	}

	return hr_res;

}

static int avb_playback_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *hw_params)
{
	struct avbcard *avbcard = snd_pcm_substream_chip(substream);

	printk(KERN_NOTICE "avb_playback_hw_params numbytes:%d sr:%d", params_buffer_bytes(hw_params), params_rate(hw_params));

	avbcard->tx.substream = substream;
	avbcard->tx.st = 0;
	avbcard->tx.sr = params_rate(hw_params);
	avbcard->tx.hw_idx = 0;
	avbcard->tx.seq_no = 0;
	avbcard->tx.hwnw_idx = 0;
	avbcard->tx.fill_size = 0;
	avbcard->tx.last_timer_ts = jiffies;
	avbcard->tx.socket_count = 0;
	avbcard->tx.pending_tx_frames = 0;
	avbcard->tx.num_bytes_consumed = 0;
	avbcard->tx.period_size = params_period_size(hw_params);
	avbcard->tx.buffer_size = params_buffer_bytes(hw_params);
	avbcard->tx.frame_count = params_buffer_size(hw_params);
	avbcard->tx.frame_size  = params_buffer_bytes(hw_params) / params_buffer_size(hw_params);

	avb_avtp_aaf_header_init(&avbcard->sd.tx_buf[0], substream, hw_params);

	hrtimer_init((struct hrtimer*)&avbdevice.tx_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	avbdevice.tx_timer.timer.function = &avb_avtp_timer;
	avbdevice.tx_timer.card = avbcard;

	memset(&avbdevice.tx_ts[0], 0, (sizeof(int) * AVB_MAX_TS_SLOTS));
	avbdevice.tx_idx = 0;

	avbcard->tx.tmp_buf = kmalloc(avbcard->tx.buffer_size, GFP_KERNEL);

	return 0;
}

static int avb_playback_hw_free(struct snd_pcm_substream *substream)
{
	struct avbcard *avbcard = snd_pcm_substream_chip(substream);

	printk(KERN_NOTICE "avb_playback_hw_free");

	hrtimer_try_to_cancel((struct hrtimer*)&avbdevice.tx_timer);

	kfree(avbcard->tx.tmp_buf);

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
	int avtp_max_frames_per_packet = 0;
	struct avbcard *avbcard = snd_pcm_substream_chip(substream);

        switch (cmd) {
        case SNDRV_PCM_TRIGGER_START:
			avbcard->tx.st = 1;
			printk(KERN_NOTICE "avb_playback_trigger: Start @ %lu", jiffies);

			avtp_max_frames_per_packet = ((ETH_DATA_LEN - sizeof(struct avt_pdu_aaf_pcm_hdr)) / avbcard->tx.frame_size);
			avbcard->tx.timer_val = ((avtp_max_frames_per_packet * 1000000u) / (avbcard->tx.sr / 1000));
			kt = ktime_set(0, avbcard->tx.timer_val);
			hrtimer_start((struct hrtimer*)&avbdevice.tx_timer, kt, HRTIMER_MODE_REL);

			break;
        case SNDRV_PCM_TRIGGER_STOP:
            printk(KERN_NOTICE "avb_playback_trigger: Stop @ %lu", jiffies);
			avbcard->tx.st = 0;

			hrtimer_try_to_cancel((struct hrtimer*)&avbdevice.tx_timer);

                break;
        default:
			printk(KERN_WARNING "avb_playback_trigger: Unknown");
			ret = -EINVAL;
        }

        return ret;
}

static snd_pcm_uframes_t avb_playback_pointer(struct snd_pcm_substream *substream)
{
	struct avbcard *avbcard = snd_pcm_substream_chip(substream);

	printk(KERN_INFO "avb_playback_pointer hw_idx:%lu numBytes:%lu, time: %u us",
		avbcard->tx.hw_idx, avbcard->tx.num_bytes_consumed, jiffies_to_usecs(jiffies - avbcard->tx.start_ts));

	return avbcard->tx.hw_idx;
}

static int avb_playback_copy(struct snd_pcm_substream *substream, int channel,
			 unsigned long pos, void __user *src,
			 unsigned long bytes)
{
	int err = 0;
	snd_pcm_uframes_t frame_pos = 0, count = 0;
	struct avbcard *avbcard = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	frame_pos = bytes_to_frames(runtime, pos);
	count = bytes_to_frames(runtime, bytes);

	printk(KERN_INFO "avb_playback_copy: ch:%d, pos: %ld, count: %lu", channel, frame_pos, count);

	if((err = copy_from_user(&avbcard->tx.tmp_buf[(frame_pos * avbcard->tx.frame_size)], src, (count * avbcard->tx.frame_size))) != 0) {
		printk(KERN_WARNING "avb_playback_copy copy from user fails: %d \n", err);
		return -1;
	}

	avbcard->tx.pending_tx_frames  += count;
	avbcard->tx.num_bytes_consumed += frames_to_bytes(runtime, count);

	return 0;
}

static int avb_capture_open(struct snd_pcm_substream *substream)
{
	printk(KERN_NOTICE "avb_capture_open");

        substream->runtime->hw = avb_capture_hw;

	return 0;
}

static int avb_capture_close(struct snd_pcm_substream *substream)
{
	printk(KERN_NOTICE "avb_capture_close");

	return 0;
}

static int avb_capture_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *hw_params)
{
	struct avbcard *avbcard = snd_pcm_substream_chip(substream);

	avbcard->rx.substream = substream;
	avbcard->rx.hw_idx = 0;
	avbcard->rx.seq_no = 0;
	avbcard->rx.hwnw_idx = 0;
	avbcard->rx.fill_size = 0;
	avbcard->rx.prev_hw_idx = 0;
	avbcard->rx.socket_count = 0;
	avbcard->rx.num_bytes_consumed = 0;
	avbcard->rx.period_size = params_period_size(hw_params);
	avbcard->rx.buffer_size = params_buffer_bytes(hw_params);
	avbcard->rx.frame_count = params_buffer_size(hw_params);
	avbcard->rx.frame_size  = params_buffer_bytes(hw_params) / params_buffer_size(hw_params);

	printk(KERN_NOTICE "avb_capture_hw_params buffer_size:%lu frame_size:%lu", avbcard->rx.buffer_size, avbcard->rx.frame_size);

	memset(&avbdevice.rx_ts[0], 0, (sizeof(int) * AVB_MAX_TS_SLOTS));
	avbdevice.rx_idx = 0;

	avbdevice.avtpwd = avb_init_and_queue_work(AVB_DELAY_WORK_AVTP, (void*)avbcard, 1);

	avbcard->rx.tmp_buf = kmalloc(avbcard->rx.buffer_size, GFP_KERNEL);

	return 0;
}

static int avb_capture_hw_free(struct snd_pcm_substream *substream)
{
	struct avbcard *avbcard = snd_pcm_substream_chip(substream);

	printk(KERN_NOTICE "avb_capture_hw_free");

	if(avbdevice.avtpwd != NULL) {
		cancel_delayed_work((struct delayed_work*)avbdevice.avtpwd);
		kfree(avbdevice.avtpwd);
		avbdevice.avtpwd = NULL;
	}

	kfree(avbcard->rx.tmp_buf);

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
			printk(KERN_NOTICE "avb_capture_trigger: Start @ %lu", jiffies);
			avbcard->rx.start_ts = jiffies;
			break;
        case SNDRV_PCM_TRIGGER_STOP:
			printk(KERN_NOTICE "avb_capture_trigger: Stop");
			break;
        default:
			printk(KERN_WARNING "avb_capture_trigger: Unknown");
			ret = -EINVAL;
        }

        return ret;
}

static snd_pcm_uframes_t avb_capture_pointer(struct snd_pcm_substream *substream)
{
	struct avbcard *avbcard = snd_pcm_substream_chip(substream);

	printk(KERN_INFO "avb_capture_pointer hw_idx:%lu numBytes:%lu",
		avbcard->rx.hw_idx, avbcard->rx.num_bytes_consumed);

	return avbcard->rx.hw_idx;
}

static int avb_capture_copy(struct snd_pcm_substream *substream, int channel,
			 unsigned long pos, void __user *dst,
			 unsigned long bytes)
{
	u8* src_buf;
	int copyres = 0;
	snd_pcm_uframes_t frame_pos = 0, count = 0;
	struct avbcard *avbcard = snd_pcm_substream_chip(substream);

	struct snd_pcm_runtime *runtime = substream->runtime;
	frame_pos = bytes_to_frames(runtime, pos);
	count = bytes_to_frames(runtime, bytes);

	src_buf = (u8*)&avbcard->rx.tmp_buf[frame_pos * avbcard->rx.frame_size];
	
	copyres = copy_to_user(dst, src_buf, (count * avbcard->rx.frame_size));

	printk(KERN_INFO "avb_capture_copy: ch:%d, pos: %ld, ct: %ld, res: %d", channel, frame_pos, count, copyres);

	avbcard->rx.num_bytes_consumed += (count * avbcard->rx.frame_size);

	return bytes;
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
	printk(KERN_NOTICE "avb_hwdep_open");

	return 0;
}

static int avb_hwdep_ioctl(struct snd_hwdep * hw, struct file *file, unsigned int cmd, unsigned long arg)
{
	int res = 0;

	if(cmd == 0) {
		printk(KERN_INFO "avb_hwdep_ioctl set ts: %ld @ idx: %d", arg, avbdevice.tx_idx);
		avbdevice.tx_ts[avbdevice.tx_idx] = arg;
		avbdevice.tx_idx++;
		avbdevice.tx_idx %= AVB_MAX_TS_SLOTS;
	} else {
		res = copy_to_user((void*)arg, &avbdevice.rx_ts[avbdevice.rx_idx], sizeof(unsigned long));
		printk(KERN_INFO "avb_hwdep_ioctl get ts: %d @ %d, res: %d", avbdevice.rx_ts[avbdevice.rx_idx], avbdevice.rx_idx, res);
		avbdevice.rx_idx++;
		avbdevice.rx_idx %= AVB_MAX_TS_SLOTS;
	}

	return 0;
}

static int avb_hwdep_release(struct snd_hwdep * hw, struct file *file)
{
	printk(KERN_NOTICE "avb_hwdep_release");

	return 0;
}

static int avb_probe(struct platform_device *devptr)
{
	struct snd_card *card;
	struct avbcard *avbcard;
	int dev = devptr->id;
	int err;

	printk(KERN_NOTICE "avb_probe");

	err = snd_card_new(&devptr->dev, index[dev], id[dev], THIS_MODULE,
			   sizeof(struct avbcard), &card);

	if (err < 0) {
		printk(KERN_ERR "avb_probe card new err: %d", err);
		return err;
	}

	avbcard = card->private_data;
	avbcard->card = card;

	err = avb_pcm_new(avbcard, 0, pcm_substreams[dev]);
	if (err < 0) {
		printk(KERN_ERR "avb_probe card pcm new err: %d", err);
		goto __nodev;
	}

	err = snd_hwdep_new(card, "avbhw", 0, &avbdevice.hwdep);
	if(err < 0) {
		printk(KERN_ERR "avb_probe card hwdep new err: %d", err);
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
			printk(KERN_ERR "avb_probe socket init failed");
			err = -1;
			goto __nodev;	
		}		

		return 0;
	}

	printk(KERN_INFO "avb_probe card reg err: %d", err);

__nodev:
	snd_card_free(card);

	return err;
}

static int avb_remove(struct platform_device *devptr)
{
	printk(KERN_NOTICE "avb_remove");
	__avb_ptp_clock.ptp_clock.unregister_clock(&__avb_ptp_clock.ptp_clock);
	snd_card_free(platform_get_drvdata(devptr));
	return 0;
}

static void avb_remove_all(void) {
	int i = 0;

	printk(KERN_NOTICE "avb_remove_all");

	for(i=0; i < numcards; i++)
		platform_device_unregister(avbdevices[i]);
}											

static int __init alsa_avb_init(void)
{
	int i, err;
	struct platform_device *dev;
	printk(KERN_NOTICE "alsa_avb_init");

	err = platform_driver_register(&avb_driver);
	if (err < 0) {
		printk(KERN_ERR "alsa_avb_init reg err %d", err);
		return err;
	}

	for(i=0; i < SND_AVB_NUM_CARDS; i++) {
		if(!enable[i])
			continue;

		dev = platform_device_register_simple(SND_AVB_DRIVER, i, NULL, 0);

		if (IS_ERR(dev)) {		
			printk(KERN_ERR "alsa_avb_init regsimple err");
			continue;
		}

		if (!platform_get_drvdata(dev)) {
			printk(KERN_ERR "alsa_avb_init getdrvdata err");
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
			printk(KERN_ERR "alsa_avb_init workqueue creation failed");
			return -1;
		}

		avbdevice.msrpwd = avb_init_and_queue_work(AVB_DELAY_WORK_MSRP, (void*)&avbdevice.msrp, 100);
		avbdevice.avdeccwd = avb_init_and_queue_work(AVB_DELAY_WORK_AVDECC, (void*)&avbdevice.avdecc, 100);

		printk(KERN_NOTICE "alsa_avb_init done err: %d, numcards: %d", err, numcards);	
	}

	return 0;
}

static void __exit alsa_avb_exit(void)
{
	printk(KERN_NOTICE "alsa_avb_exit");
	
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
	
	printk(KERN_NOTICE "alsa_avb_exit done");
}

module_init(alsa_avb_init)
module_exit(alsa_avb_exit)

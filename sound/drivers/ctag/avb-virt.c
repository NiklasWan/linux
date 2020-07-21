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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/kernel.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/initval.h>

#include "avb-virt.h"

MODULE_AUTHOR("Niklas Wantrupp <niklaswantrupp@web.de>");
MODULE_DESCRIPTION("Virtual AVB ALSA sound device");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{ALSA,Virtual AVB souind device}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static bool enable[SNDRV_CARDS] = {1, [1 ... (SNDRV_CARDS - 1)] = 0};
static int pcm_substreams[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 8};
static int pcm_notify[SNDRV_CARDS];

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for virtual ALSA AVB sound device.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for virtual ALSA AVB sound device.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable this loopback soundcard.");
module_param_array(pcm_substreams, int, NULL, 0444);
MODULE_PARM_DESC(pcm_substreams, "PCM substreams # (1-8) for virtual ALSA AVB sound device.");
module_param_array(pcm_notify, int, NULL, 0444);
MODULE_PARM_DESC(pcm_notify, "Break capture when PCM format/rate/channels changes.");

#define RATE_MIN 8000
#define RATE_MAX 192000
#define CHANNELS_MIN 1
#define CHANNELS_MAX 8
#define PERIODS_MIN 1
#define PERIODS_MAX 32
#define PERIOD_BYTES_MIN 48
#define PERIOD_BYTES_MAX PERIOD_BYTES_MIN
#define MAX_BUFFER (PERIODS_MAX * PERIOD_BYTES_MAX)

#define SND_AVB_VIRT_DRIVER	"snd_avb_virt"

static struct snd_pcm_hardware avb_virt_pcm_hw = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_BLOCK_TRANSFER),
	.formats          = SNDRV_PCM_FMTBIT_S16_LE,
	.rates            = SNDRV_PCM_RATE_8000_192000,
	.rate_min         = RATE_MIN,
	.rate_max         = RATE_MAX,
	.channels_min     = CHANNELS_MIN,
	.channels_max     = CHANNELS_MAX,
	.buffer_bytes_max = MAX_BUFFER,
	.period_bytes_min = PERIOD_BYTES_MIN,
	.period_bytes_max = PERIOD_BYTES_MAX,
	.periods_min      = PERIODS_MIN,
	.periods_max      = PERIODS_MAX,
};

struct avb_virt_device {
	struct snd_card *card;
	struct snd_pcm *pcm;
};

static void avb_virt_unregister_all(void);
static int __init alsa_card_avb_virt_init(void);
static void __exit alsa_card_avb_virt_exit(void);

static int avb_virt_probe(struct platform_device *devptr);
static int avb_virt_remove(struct platform_device *devptr);

static int avb_virt_playback_hw_params(struct snd_pcm_substream *ss,
				       struct snd_pcm_hw_params *hw_params);
static int avb_virt_playback_hw_free(struct snd_pcm_substream *ss);
static int avb_virt_playback_pcm_open(struct snd_pcm_substream *ss);
static int avb_virt_playback_pcm_close(struct snd_pcm_substream *ss);
static int avb_virt_playback_pcm_prepare(struct snd_pcm_substream *ss);
static int avb_virt_playback_pcm_trigger(struct snd_pcm_substream *ss,
					 int cmd);
static snd_pcm_uframes_t avb_virt_playback_pcm_pointer(struct snd_pcm_substream *ss);

static int avb_virt_capture_hw_params(struct snd_pcm_substream *ss,
			      struct snd_pcm_hw_params *hw_params);
static int avb_virt_capture_hw_free(struct snd_pcm_substream *ss);
static int avb_virt_capture_pcm_open(struct snd_pcm_substream *ss);
static int avb_virt_capture_pcm_close(struct snd_pcm_substream *ss);
static int avb_virt_capture_pcm_prepare(struct snd_pcm_substream *ss);
static int avb_virt_capture_pcm_trigger(struct snd_pcm_substream *ss,
				int cmd);
static snd_pcm_uframes_t avb_virt_capture_pcm_pointer(struct snd_pcm_substream *ss);

static int avb_virt_pcm_dev_free(struct snd_device *device);
static int avb_virt_pcm_free(struct avb_virt_device *dev);

static void avb_virt_timer_start(struct avb_virt_device *dev);
static void avb_virt_timer_stop(struct avb_virt_device *dev);
static void avb_virt_pos_update(struct avb_virt_device *dev);
static void avb_virt_timer_function(struct timer_list *t);
static void avb_virt_xfer_buf(struct avb_virt_device *dev, unsigned int count);
static void avb_virt_fill_capture_buf(struct avb_virt_device *dev, unsigned int bytes);

static struct snd_pcm_ops avb_virt_playback_ops = {
	.open      = avb_virt_playback_pcm_open,
	.close     = avb_virt_playback_pcm_close,
	.ioctl     = snd_pcm_lib_ioctl,
	.hw_params = avb_virt_playback_hw_params,
	.hw_free   = avb_virt_playback_hw_free,
	.prepare   = avb_virt_playback_pcm_prepare,
	.trigger   = avb_virt_playback_pcm_trigger,
	.pointer   = avb_virt_playback_pcm_pointer,
};

static struct snd_pcm_ops avb_virt_capture_ops = {
	.open      = avb_virt_capture_pcm_open,
	.close     = avb_virt_capture_pcm_close,
	.ioctl     = snd_pcm_lib_ioctl,
	.hw_params = avb_virt_capture_hw_params,
	.hw_free   = avb_virt_capture_hw_free,
	.prepare   = avb_virt_capture_pcm_prepare,
	.trigger   = avb_virt_capture_pcm_trigger,
	.pointer   = avb_virt_capture_pcm_pointer,
};

static struct snd_device_ops dev_ops = {
	.dev_free = avb_virt_pcm_dev_free,
};

static struct platform_driver avb_virt_driver = {
	.probe		= avb_virt_probe,
	.remove		= avb_virt_remove,
	.driver		= {
		.name	= SND_AVB_VIRT_DRIVER,
		.owner  = THIS_MODULE
	},
};

static int avb_virt_probe(struct platform_device *devptr) {
	printk(KERN_DEBUG "Called AVB Virtual Probe");
	return 0;
}

static int avb_virt_remove(struct platform_device *devptr) {
	printk(KERN_DEBUG "Called AVB Virtual Remove");
	return 0;
}

static int avb_virt_playback_hw_params(struct snd_pcm_substream *ss,
			      struct snd_pcm_hw_params *hw_params) {
	printk(KERN_DEBUG "Called AVB Virtual Playback HW Params");
	return 0;
}

static int avb_virt_playback_hw_free(struct snd_pcm_substream *ss) {
	printk(KERN_DEBUG "Called AVB Virtual Playback HW Free");
	return 0;
}

static int avb_virt_playback_pcm_open(struct snd_pcm_substream *ss) {
	printk(KERN_DEBUG "Called AVB Virtual Playback PCM Open");
	return 0;
}

static int avb_virt_playback_pcm_close(struct snd_pcm_substream *ss) {
	printk(KERN_DEBUG "Called AVB Virtual Playback PCM Close");
	return 0;
}

static int avb_virt_playback_pcm_prepare(struct snd_pcm_substream *ss) {
	printk(KERN_DEBUG "Called AVB Virtual Playback PCM Prepare");
	return 0;
}

static int avb_virt_playback_pcm_trigger(struct snd_pcm_substream *ss,
				int cmd) {
	printk(KERN_DEBUG "Called AVB Virtual Playback PCM Trigger");
	return 0;
}

static snd_pcm_uframes_t avb_virt_playback_pcm_pointer(struct snd_pcm_substream *ss) {
	printk(KERN_DEBUG "Called AVB Virtual Playback PCM Pointer");
	return 0;
}

static int avb_virt_capture_hw_params(struct snd_pcm_substream *ss,
				       struct snd_pcm_hw_params *hw_params) {
	printk(KERN_DEBUG "Called AVB Virtual Capture HW Params");
	return 0;
}

static int avb_virt_capture_hw_free(struct snd_pcm_substream *ss) {
	printk(KERN_DEBUG "Called AVB Virtual Capture HW Free");
	return 0;
}

static int avb_virt_capture_pcm_open(struct snd_pcm_substream *ss) {
	printk(KERN_DEBUG "Called AVB Virtual Capture PCM Open");
	return 0;
}

static int avb_virt_capture_pcm_close(struct snd_pcm_substream *ss) {
	printk(KERN_DEBUG "Called AVB Virtual Capture PCM Close");
	return 0;
}

static int avb_virt_capture_pcm_prepare(struct snd_pcm_substream *ss) {
	printk(KERN_DEBUG "Called AVB Virtual Capture PCM Prepare");
	return 0;
}

static int avb_virt_capture_pcm_trigger(struct snd_pcm_substream *ss,
					 int cmd) {
	printk(KERN_DEBUG "Called AVB Virtual Capture PCM Trigger");
	return 0;
}

static snd_pcm_uframes_t avb_virt_capture_pcm_pointer(struct snd_pcm_substream *ss) {
	printk(KERN_DEBUG "Called AVB Virtual Capture PCM Pointer");
	return 0;
}

static void avb_virt_timer_start(struct avb_virt_device *dev) {
	printk(KERN_DEBUG "Called AVB Virtual Timer Start");
	//TODO
}

static void avb_virt_timer_stop(struct avb_virt_device *dev) {
	printk(KERN_DEBUG "Called AVB Virtual Timer Stop");
	// TODO
}

static void avb_virt_pos_update(struct avb_virt_device *dev) {
	printk(KERN_DEBUG "Called AVB Virtual Pos Update");
	// TODO
}

static void avb_virt_timer_function(struct timer_list *t) {
	printk(KERN_DEBUG "Called AVB Virtual Timer Function");
	// TODO
}

static void avb_virt_xfer_buf(struct avb_virt_device *dev, unsigned int count) {
	printk(KERN_DEBUG "Called AVB Virtual Xfer Buffer");
	// TODO
}

static void avb_virt_fill_capture_buf(struct avb_virt_device *dev, unsigned int bytes) {
	printk(KERN_DEBUG "Called AVB Virtual Fill Capture Buffer");
	// TODO
}

static int avb_virt_pcm_free(struct avb_virt_device *dev) {
	printk(KERN_DEBUG "Called AVB Virtual PCM Free");
	return 0;
}

static int avb_virt_pcm_dev_free(struct snd_device *device) {
	printk(KERN_DEBUG "Called AVB Virtual PCM Device Free");
	return 0;
}

static void avb_virt_unregister_all(void) {
	printk(KERN_DEBUG "Called AVB Virtual Unregister All");
	// TODO
}

static int __init alsa_card_avb_virt_init(void) {
	printk(KERN_DEBUG "Called AVB Virtual Init");
	return 0;
}

static void __exit alsa_card_avb_virt_exit(void) {
	printk(KERN_DEBUG "Called AVB Virtual Exit");
	// TODO
}

module_init(alsa_card_avb_virt_init)
module_exit(alsa_card_avb_virt_exit)

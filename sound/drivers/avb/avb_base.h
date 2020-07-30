#ifndef AVB_BASE_H
#define AVB_BASE_H

#include "msrp.h"
#include "avtp.h"
#include "avdecc.h"

struct avbdevice {
	int tx_ts[AVB_MAX_TS_SLOTS];
	int rx_ts[AVB_MAX_TS_SLOTS];
	int tx_idx;
	int rx_idx;
	struct msrp msrp;
	struct avdecc avdecc;
	struct avbcard card;
	struct snd_hwdep *hwdep;
	struct avbhrtimer tx_timer;
	struct workdata* avdeccwd;
	struct workdata* msrpwd;
	struct workdata* avtpwd;
	struct workqueue_struct* wq;
};

struct workdata {
	struct avbdevice *device;
	struct delayed_work work;
	int delayed_work_id;
	union delayed_work_data {
		void* data;
		struct msrp* msrp;
		struct avbcard* card;
		struct avdecc* avdecc;
	} dw;
};

#endif
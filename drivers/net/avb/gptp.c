#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/net.h>
#include <asm-generic/errno-base.h>

#include "gptp_common.h"

#define TIMEOUT 100 //ms

MODULE_DESCRIPTION("Kernel Dummy Module");
MODULE_AUTHOR("Niklas Wantrupp");
MODULE_LICENSE("GPL");

void gptp_timer_callback(struct timer_list *data);
void gptp_worker_fn(struct work_struct *work);

static int count = 0;
static struct timer_list gptp_callback_timer;
static struct workqueue_struct *gptp_workqueue;
static DECLARE_WORK(gptp_work, gptp_worker_fn);

void gptp_timer_callback(struct timer_list *data)
{
	count++;
	printk(KERN_INFO "Timer callback fired. Count: %d\n", count);

	mod_timer(&gptp_callback_timer, jiffies + msecs_to_jiffies(TIMEOUT));
}

void gptp_worker_fn(struct work_struct *work)
{
	printk(KERN_INFO "Hello from the workqueue\n");

	timer_setup(&gptp_callback_timer, gptp_timer_callback, 0);
	mod_timer(&gptp_callback_timer, jiffies + msecs_to_jiffies(TIMEOUT));
	//queue_delayed_work(gptp_workqueue, &gptp_work, msecs_to_jiffies(TIMEOUT));
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

/* Alessandro Rubini for CERN 2014, GPLv2 or later */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/vmalloc.h>
#include <linux/clocksource.h>

/* We need these two defined in order to include nic-hardware.h */
#define WR_IS_NODE 0
#define WR_IS_SWITCH 1

/* We have no centralized defines yet: pick frequency and registers base */
#include "../wr_nic/nic-hardware.h"
#define WRCS_FREQUENCY		REFCLK_FREQ
#define WRCS_TICK_NS		(NSEC_PER_SEC / WRCS_FREQUENCY)

static int wrcs_stats;
module_param(wrcs_stats, int, 0644);
MODULE_PARM_DESC(wrcs_stats, "Count how often the clocksource is being read");

static __iomem struct PPSG_WB *wrcs_ppsg;

static int wrcs_is_registered; /* no need for atomic_t or whatever */

/* prototypes */
static int pps_gen_busy(void);
static cycle_t wrcs_read(struct clocksource *cs);

static struct clocksource wrcs_cs = {
	.name = "white-rabbit",
	.rating = 450,		/* perfect... */
	.read = wrcs_read,
	/* no enable/disable */
	.mask = 0xffffffff, /* We fake a 32-bit thing */
	.max_idle_ns = 900 * 1000 * 1000, /* well, 1s... */
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
};


static int pps_gen_busy(void)
{
	uint32_t cr = readl(&wrcs_ppsg->CR);
	return cr & PPSG_CR_CNT_ADJ ? 0 : 1;
}


/* If so requested, print statistics once per second */
static inline void wrcs_do_stats(void)
{
	static unsigned long nextp;
	static int ncalls;

	if (!wrcs_stats)
		return;

	if (!nextp)
		nextp = jiffies + HZ;

	/* This, when enabled, shows around 400 calls per second */
	if (time_after_eq(jiffies, nextp)) {
		pr_info("%s: called %i times\n", __func__,
			ncalls);
		ncalls = 0;
		nextp += HZ;
	}
	ncalls++;
}


/*
 * The timer is used to check when does WR synchronize.  When that
 * happens, we set time of day and register our clocksource. Time
 * jumps after synchronization are not well supported.
 */
static void wrcs_timer_fn(unsigned long unused);
static DEFINE_TIMER(wrcs_timer, wrcs_timer_fn, 0, 0);

static void wrcs_timer_fn(unsigned long unused)
{
	uint32_t ticks, tai_l, tai_h;
	int64_t tai;

	if ( pps_gen_busy() ){
		printk(KERN_DEBUG " %s: Adjusting in progress \n",__func__);
		/* NS counter adjustment in progress. Try later */
		goto retryLater;
	}

	wrcs_cs.rating=450; /* Perfect */

	/* Read ppsg, all fields consistently so we can use the value */
	do {
		tai_l = readl(&wrcs_ppsg->CNTR_UTCLO);
		tai_h = readl(&wrcs_ppsg->CNTR_UTCHI);
		ticks = readl(&wrcs_ppsg->CNTR_NSEC);
	} while (readl(&wrcs_ppsg->CNTR_UTCLO) != tai_l);
	tai = (typeof(tai))tai_h << 32 | tai_l;

	/* If we are before 2010 (date +%s --date=2010-01-01), try again */
	if (tai < 1262300400LL)
		goto retryLater;

	clocksource_register_hz(&wrcs_cs, WRCS_FREQUENCY);
	wrcs_is_registered = 1;
	/* And don't restart the timer */
	return;

retryLater:;
	wrcs_cs.rating=450; /* Perfect */
	mod_timer(&wrcs_timer, jiffies + HZ);
	return;

}

DEFINE_SPINLOCK(wrcs_lock);

static cycle_t wrcs_read(struct clocksource *cs)
{
	static uint32_t offset, last, this, adjustOffset;
	static char cntWasNeg;
	int32_t ticksCounter;
	unsigned long flags;

	wrcs_do_stats();

	/* FIXME: identify a time jump by monitoring the tick counter */

	/*
	 * Turn the counter into a 32-bit one (see cs->mask below).
	 * We reset at 0x3b9aca0, so without this we should use mask = 0x1f
	 * and mac_idle = 32 ticks = 512ns. Unaffordable.
	 */
	spin_lock_irqsave(&wrcs_lock, flags);

	ticksCounter = readl(&wrcs_ppsg->CNTR_NSEC);
	if ( (ticksCounter & 0x8000000)!=0 ) {
		// Negative value
		ticksCounter|= 0xF0000000;
		if ( ! cntWasNeg ) {
			/* Jump from positive to negative counter value */
			adjustOffset+=-ticksCounter;
			adjustOffset%=WRCS_FREQUENCY;
			cntWasNeg=1;
		}
	} else {
		/* the counter is positive */
		cntWasNeg=0;
	}
	this=(ticksCounter+adjustOffset)%WRCS_FREQUENCY;
	if ( this < last) {
		offset += WRCS_FREQUENCY;
	}
	last = this;
	spin_unlock_irqrestore(&wrcs_lock, flags);
	return offset + this;
}

static int wrcs_init(void)
{
	wrcs_ppsg = ioremap(FPGA_BASE_PPSG, FPGA_SIZE_PPSG);
	if (!wrcs_ppsg) {
		pr_err("WR Clocksource: can't remap PPS registers\n");
		return -EIO;
	}

	/* Fire the timer */
	mod_timer(&wrcs_timer, jiffies + HZ);
	return 0;
}

static void wrcs_exit(void)
{
	del_timer_sync(&wrcs_timer);
	if (wrcs_is_registered)
		clocksource_unregister(&wrcs_cs);
	iounmap(wrcs_ppsg);
}


module_init(wrcs_init);
module_exit(wrcs_exit);

MODULE_LICENSE("GPL");


/* Hack: this is not exported by current kernel. Define a local copy */
void
clocks_calc_mult_shift(u32 *mult, u32 *shift, u32 from, u32 to, u32 maxsec)
{
	u64 tmp;
	u32 sft, sftacc= 32;

	/*
	 * Calculate the shift factor which is limiting the conversion
	 * range:
	 */
	tmp = ((u64)maxsec * from) >> 32;
	while (tmp) {
		tmp >>=1;
		sftacc--;
	}

	/*
	 * Find the conversion shift/mult pair which has the best
	 * accuracy and fits the maxsec conversion range:
	 */
	for (sft = 32; sft > 0; sft--) {
		tmp = (u64) to << sft;
		tmp += from / 2;
		do_div(tmp, from);
		if ((tmp >> sftacc) == 0)
			break;
	}
	*mult = tmp;
	*shift = sft;
}


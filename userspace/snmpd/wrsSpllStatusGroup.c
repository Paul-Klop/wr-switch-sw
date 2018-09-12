#include "wrsSnmp.h"
#include <libwr/softpll.h>
#include <libwr/util.h>
#include "wrsSpllStatusGroup.h"

static struct spll_stats *spll_stats_p;

static struct pickinfo wrsSpllStatus_pickinfo[] = {
	FIELD(wrsSpllStatus_s, ASN_INTEGER, wrsSpllMode),
	FIELD(wrsSpllStatus_s, ASN_COUNTER, wrsSpllIrqCnt),
	FIELD(wrsSpllStatus_s, ASN_INTEGER, wrsSpllSeqState),
	FIELD(wrsSpllStatus_s, ASN_INTEGER, wrsSpllAlignState),
	FIELD(wrsSpllStatus_s, ASN_COUNTER, wrsSpllHlock),
	FIELD(wrsSpllStatus_s, ASN_COUNTER, wrsSpllMlock),
	FIELD(wrsSpllStatus_s, ASN_INTEGER, wrsSpllHY),
	FIELD(wrsSpllStatus_s, ASN_INTEGER, wrsSpllMY),
	FIELD(wrsSpllStatus_s, ASN_COUNTER, wrsSpllDelCnt),
};

struct wrsSpllStatus_s wrsSpllStatus_s;

time_t wrsSpllStatus_data_fill(void)
{
	static time_t time_update;
	time_t time_cur;

	time_cur = get_monotonic_sec();
	if (time_update
	    && time_cur - time_update < WRSSPLLSTATUS_CACHE_TIMEOUT) {
		/* cache not updated, return last update time */
		return time_update;
	}
	time_update = time_cur;

	memset(&wrsSpllStatus_s, 0, sizeof(wrsSpllStatus_s));

	if (!spll_stats_p) /* first time, map the fpga space */
		spll_stats_p = create_map(FPGA_SPLL_STAT,
					  sizeof(*spll_stats_p));
	if (!spll_stats_p) {
		/* unable to mmap */
		return time_update;
	}
	/* check magic number in SPLL stat memory */
	if (spll_stats_p->magic != SPLL_MAGIC) {
		/* wrong magic */
		snmp_log(LOG_ERR,"SNMP: " SL_ER
			 "wrsSpllStatusGroup Wrong SPLL magic number\n");
		return time_update;
	}
	/* check version of SPLL's stat structure, versions 1 and 2 are ok */
	if (spll_stats_p->ver <= 4 ) {
		wrsSpllStatus_s.wrsSpllMode = spll_stats_p->mode;
		wrsSpllStatus_s.wrsSpllIrqCnt = spll_stats_p->irq_cnt;
		wrsSpllStatus_s.wrsSpllSeqState = spll_stats_p->seq_state;
		wrsSpllStatus_s.wrsSpllAlignState = spll_stats_p->align_state;
		wrsSpllStatus_s.wrsSpllHlock = spll_stats_p->H_lock;
		wrsSpllStatus_s.wrsSpllMlock = spll_stats_p->M_lock;
		wrsSpllStatus_s.wrsSpllHY = spll_stats_p->H_y;
		wrsSpllStatus_s.wrsSpllMY = spll_stats_p->M_y;
		wrsSpllStatus_s.wrsSpllDelCnt = spll_stats_p->del_cnt;
	}
	else
	{
		snmp_log(LOG_ERR, "SNMP: " SL_ER
			"wrsSpllStatusGroup unsupported version of spll_stats "
			"registers (reading ver %d, supported 1, 2 and 3) \n",
			spll_stats_p->ver);
	}
	/* there was an update, return current time */
	return time_update;
}

#define GT_OID WRSSPLLSTATUS_OID
#define GT_PICKINFO wrsSpllStatus_pickinfo
#define GT_DATA_FILL_FUNC wrsSpllStatus_data_fill
#define GT_DATA_STRUCT wrsSpllStatus_s
#define GT_GROUP_NAME "wrsSpllStatusGroup"
#define GT_INIT_FUNC init_wrsSpllStatusGroup

#include "wrsGroupTemplate.h"

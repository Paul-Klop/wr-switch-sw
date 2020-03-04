#include "wrsSnmp.h"
#include <libwr/util.h>
#include <libwr/config.h>
#include <snmp_shmem.h>
#include "wrsPtpDataTable.h"
#include "wrsSpllStatusGroup.h"
#include "wrsPortStatusTable.h"
#include "wrsPtpInstanceTable.h"
#include "wrsTimingStatusGroup.h"
#include "wrsCurrentTimeGroup.h"

/* Macros for fscanf function to read line with maximum of "x" characters
 * without new line. Macro expands to something like: "%10[^\n]" */
#define LINE_READ_LEN_HELPER(x) "%"#x"[^\n]"
#define LINE_READ_LEN(x) LINE_READ_LEN_HELPER(x)

#define WRS_SYSTEMCLOCK_STATUS_CACHE_TIMEOUT 20 /* 20 seconds */
#define WRS_LEAPSEC_STATUS_CACHE_TIMEOUT    20 /* 20 seconds */
#define WRS_LEAPSEC_DOWNLOAD_CACHE_TIMEOUT    20 /* 20 seconds */

static struct pickinfo wrsTimingStatus_pickinfo[] = {
	FIELD(wrsTimingStatus_s, ASN_INTEGER, wrsPTPStatus),
	FIELD(wrsTimingStatus_s, ASN_INTEGER, wrsSoftPLLStatus),
	FIELD(wrsTimingStatus_s, ASN_INTEGER, wrsSlaveLinksStatus),
	FIELD(wrsTimingStatus_s, ASN_INTEGER, wrsPTPFramesFlowing),
	FIELD(wrsTimingStatus_s, ASN_INTEGER, wrsSystemClockStatus),
	FIELD(wrsTimingStatus_s, ASN_INTEGER, wrsLeapSecStatus),
	FIELD(wrsTimingStatus_s, ASN_INTEGER, wrsLeapSecSourceStatus),
};

struct wrsTimingStatus_s wrsTimingStatus_s;
static char *slog_obj_name;
static char *wrsPTPStatus_str = "wrsPTPStatus";
static char *wrsSoftPLLStatus_str = "wrsSoftPLLStatus";
static char *wrsSlaveLinksStatus_str = "wrsSlaveLinksStatus";
static char *wrsPTPFramesFlowing_str = "wrsPTPFramesFlowing";

static void get_wrsPTPStatus(unsigned int ptp_data_nrows, int t_delta);
static void get_wrsSoftPLLStatus();
static void get_wrsSlaveLinksStatus(unsigned int port_status_nrows);
static void get_wrsPTPFramesFlowing(unsigned int port_status_nrows);
static void get_wrsSystemClockStatus(void);
static void get_wrsLeapSecondStatus(void);
static void get_wrsLeapSecondSourceStatus(void);

time_t wrsTimingStatus_data_fill(void)
{
	static time_t time_update; /* time of last update */
	time_t time_ptp_data; /* time when wrsPtpDataTable was updated */
	time_t time_spll; /* time when softPLL data was updated */
	time_t time_port_status; /* time when port status table was updated */
	time_t time_ptp_instance; /* time when wrsPtpInstanceTable was updated */
	static time_t time_ptp_data_prev; /* time when previous wrsPtpDataTable
					   * table was updated */
	static time_t time_current_time; /* time when previous wrsCurrentTime was updated */

	unsigned int ptp_data_nrows; /* number of rows in wrsPtpDataTable */
	unsigned int port_status_nrows; /* number of rows in PortStatusTable */
	unsigned int ptp_instance_nrows;

	time_ptp_data = wrsPtpDataTable_data_fill(&ptp_data_nrows);
	time_spll = wrsSpllStatus_data_fill();
	time_port_status = wrsPortStatusTable_data_fill(&port_status_nrows);
	time_ptp_instance = wrsPtpInstanceTable_data_fill(&ptp_instance_nrows);
	time_current_time = wrsCurrentTime_data_fill();

	if (ptp_data_nrows > WRS_MAX_N_SERVO_INSTANCES) {
		snmp_log(LOG_ERR, "SNMP: wrsTimingStatusGroup too many PTP "
				  "instances(%d), only %d supported!\n",
			 WRS_MAX_N_SERVO_INSTANCES, ptp_data_nrows);
		ptp_data_nrows = WRS_MAX_N_SERVO_INSTANCES;
	}

	if (port_status_nrows > WRS_N_PORTS) {
		snmp_log(LOG_ERR, "SNMP: wrsTimingStatusGroup too many ports"
				  "(%d), only %d supported!\n",
			 WRS_N_PORTS, port_status_nrows);
		port_status_nrows = WRS_N_PORTS;
	}

	if (time_ptp_data <= time_update
	    && time_spll <= time_update
	    && time_port_status <= time_update
	    && time_ptp_instance <= time_update
		&& time_current_time <= time_update) {
		/* cache not updated, return last update time */
		return time_update;
	}

	/* update when ptp_data and spll status were updated
	 * otherwise there may be comparison between the same data */
	if (time_ptp_data > time_update
	    && time_spll > time_update) {
		get_wrsPTPStatus(ptp_data_nrows,
				 time_ptp_data - time_ptp_data_prev);
	}

	/* update when the spll was updated
	 * otherwise there may be comparison between the same data */
	if (time_spll > time_update) {
		get_wrsSoftPLLStatus();
	}

	/* update when port_status and ptp instances were updated
	 * otherwise there may be comparison between the same data */
	if (time_port_status > time_update && time_ptp_instance > time_update) {
		get_wrsSlaveLinksStatus(port_status_nrows);
		get_wrsPTPFramesFlowing(port_status_nrows);
	}

	if ( time_current_time > time_update) {
		get_wrsLeapSecondStatus();
		get_wrsSystemClockStatus();
		get_wrsLeapSecondSourceStatus();
	}

	/* save the time of the last ptp_data copy */
	time_ptp_data_prev = time_ptp_data;

	/* there was an update, return current time */
	return time_update=get_monotonic_sec();
}

static void get_wrsPTPStatus(unsigned int ptp_data_nrows, int t_delta)
{
	struct wrsSpllStatus_s *s;
	struct wrsPtpDataTable_s *pd_a;
	struct wrsTimingStatus_s *t;
	int i;
	static int first_run = 1;

	/* store old values of ptp servo error counters and number of updates */
	static uint32_t wrsPtpServoUpdates_prev[WRS_MAX_N_SERVO_INSTANCES];
	static uint32_t wrsPtpServoStateErrCnt_prev[WRS_MAX_N_SERVO_INSTANCES];
	static uint32_t wrsPtpClockOffsetErrCnt_prev[WRS_MAX_N_SERVO_INSTANCES];
	static uint32_t wrsPtpRTTErrCnt_prev[WRS_MAX_N_SERVO_INSTANCES];

	/*********************************************************************\
	|*************************** wrsPTPStatus  ***************************|
	\*********************************************************************/
	/*
	 * Error when SPLL is in slave mode and at least one error counter in
	 * PTP increased or no PTP servo updates
	 */
	s = &wrsSpllStatus_s;
	pd_a = wrsPtpDataTable_array;
	t = &wrsTimingStatus_s;
	slog_obj_name = wrsPTPStatus_str;

	t->wrsPTPStatus = WRS_PTP_STATUS_OK;
	/* NOTE: only one PTP instance is used right now. When switchover is
	 * implemented it will change */
	for (i = 0; i < ptp_data_nrows; i++) {
		if (first_run == 1) {
			/* don't report errors during first run */
			t->wrsPTPStatus = WRS_PTP_STATUS_FR;
			/* no need to check others */
			break;

		/* check if error */
		} else if (s->wrsSpllMode == WRS_SPLL_MODE_SLAVE) {
			if (pd_a[i].wrsPtpServoUpdates == wrsPtpServoUpdates_prev[i]) {
				t->wrsPTPStatus = WRS_PTP_STATUS_ERROR;
				snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
					 "No PTP servo update since last check (%ds)\n",
					 slog_obj_name, t_delta);
			}
			if (pd_a[i].wrsPtpServoStateErrCnt != wrsPtpServoStateErrCnt_prev[i]) {
				t->wrsPTPStatus = WRS_PTP_STATUS_ERROR;
				snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
					 "PTP servo not in TRACK_PHASE - %d times since last check (%ds)\n",
					 slog_obj_name,
					 pd_a[i].wrsPtpServoStateErrCnt - wrsPtpServoStateErrCnt_prev[i],
					 t_delta);
			}
			if (pd_a[i].wrsPtpClockOffsetErrCnt != wrsPtpClockOffsetErrCnt_prev[i]) {
				t->wrsPTPStatus = WRS_PTP_STATUS_ERROR;
				snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
					 "PTP clock offset too large - %d times since last check (%ds)\n",
					 slog_obj_name,
					 pd_a[i].wrsPtpClockOffsetErrCnt - wrsPtpClockOffsetErrCnt_prev[i],
					 t_delta);
			}
			if (pd_a[i].wrsPtpRTTErrCnt != wrsPtpRTTErrCnt_prev[i]) {
				t->wrsPTPStatus = WRS_PTP_STATUS_ERROR;
				snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
					 "Jump in RTT value - %d times since last check (%ds)\n",
					 slog_obj_name,
					 pd_a[i].wrsPtpRTTErrCnt - wrsPtpRTTErrCnt_prev[i],
					 t_delta);
			}
			if (pd_a[i].wrsPtpDeltaTxM == 0) {
				t->wrsPTPStatus = WRS_PTP_STATUS_ERROR;
				snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
					 "DeltaTx for Master set to 0\n",
					 slog_obj_name);
			}
			if (pd_a[i].wrsPtpDeltaRxM == 0) {
				t->wrsPTPStatus = WRS_PTP_STATUS_ERROR;
				snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
					 "DeltaRx for Master set to 0\n",
					 slog_obj_name);
			}
			if (pd_a[i].wrsPtpDeltaTxS == 0) {
				t->wrsPTPStatus = WRS_PTP_STATUS_ERROR;
				snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
					 "DeltaTx for Slave set to 0\n",
					 slog_obj_name);
			}
			if (pd_a[i].wrsPtpDeltaRxS == 0) {
			t->wrsPTPStatus = WRS_PTP_STATUS_ERROR;
				snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
					 "DeltaRx for Slave set to 0\n",
					 slog_obj_name);
			}
		}
	}

	for (i = 0; i < ptp_data_nrows; i++) {
		/* update old values */
		wrsPtpServoUpdates_prev[i] = pd_a[i].wrsPtpServoUpdates;
		wrsPtpServoStateErrCnt_prev[i] = pd_a[i].wrsPtpServoStateErrCnt;
		wrsPtpClockOffsetErrCnt_prev[i] = pd_a[i].wrsPtpClockOffsetErrCnt;
		wrsPtpRTTErrCnt_prev[i] = pd_a[i].wrsPtpRTTErrCnt;
	}

	first_run = 0;
}

static void get_wrsSoftPLLStatus(void)
{
	struct wrsSpllStatus_s *s;
	struct wrsTimingStatus_s *t;

	/* store old values of SPLL status */
	static int32_t spll_DelCnt_prev;
	/*********************************************************************\
	|************************* wrsSoftPLLStatus  *************************|
	\*********************************************************************/
	/*
	 * DelCnt - warning if > 0 or no change in DelCnt
	 * seqstate has to be 8 (ready)
	 * mode = 1 (grand master) aligin state must be 6 (LOCKED)
	 * mode = 2 (free running master)
	 * mode = 3 (slave) and wrsSpllHlock != 0 and wrsSpllMlock != 0
	*/
	s = &wrsSpllStatus_s;
	t = &wrsTimingStatus_s;
	slog_obj_name = wrsSoftPLLStatus_str;

	t->wrsSoftPLLStatus = 0;
	/* check if error */
	if (s->wrsSpllSeqState != WRS_SPLL_SEQ_STATE_READY) {
  		t->wrsSoftPLLStatus = WRS_SOFTPLL_STATUS_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
			 "Sequencing FSM state of SoftPLL is not READY. "
			 "SoftPLL is not yet ready or has unlocked.\n",
			 slog_obj_name);
	}
	if (s->wrsSpllMode == WRS_SPLL_MODE_GRAND_MASTER
	    && s->wrsSpllAlignState != WRS_SPLL_ALIGN_STATE_LOCKED) {
		t->wrsSoftPLLStatus = WRS_SOFTPLL_STATUS_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
			 "Alignment FSM state of SoftPLL is not LOCKED. "
			 "SoftPLL is not yet ready or has unlocked.\n",
			 slog_obj_name);
	}
	if ((s->wrsSpllMode != WRS_SPLL_MODE_GRAND_MASTER)
		    && (s->wrsSpllMode != WRS_SPLL_MODE_MASTER)
		    && (s->wrsSpllMode != WRS_SPLL_MODE_SLAVE)) {
		t->wrsSoftPLLStatus = WRS_SOFTPLL_STATUS_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
			 "SoftPLL in mode %d which is neither of the supported "
			 "modes: GrandMaster, Master, Slave\n",
			 slog_obj_name, s->wrsSpllMode);
	}
	if ((s->wrsSpllMode == WRS_SPLL_MODE_SLAVE) && (s->wrsSpllHlock == 0)){
		t->wrsSoftPLLStatus = WRS_SOFTPLL_STATUS_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
			 "SoftPLL is in Slave mode and Helper PLL is not locked\n",
			 slog_obj_name);
	}
	if ((s->wrsSpllMode == WRS_SPLL_MODE_SLAVE) && (s->wrsSpllMlock == 0)){
		t->wrsSoftPLLStatus = WRS_SOFTPLL_STATUS_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
			 "SoftPLL is in Slave mode and Main PLL is not locked\n",
			 slog_obj_name);
	}

	/* check if warning */
	if (!t->wrsSoftPLLStatus) {
		if (s->wrsSpllMode == WRS_SPLL_MODE_GRAND_MASTER && s->wrsSpllDelCnt > 0) {
			t->wrsSoftPLLStatus = WRS_SOFTPLL_STATUS_WARNING;
			snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: "
					  "SoftPLL in GrandMaster mode has unlocked from "
					  "the external reference. Delock counter is %d\n",
					  slog_obj_name, s->wrsSpllDelCnt);
		}
		if (s->wrsSpllMode == WRS_SPLL_MODE_MASTER && s->wrsSpllDelCnt != spll_DelCnt_prev) {
			t->wrsSoftPLLStatus = WRS_SOFTPLL_STATUS_WARNING;
			snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: "
					  "SoftPLL in Master mode has unlocked. Delock "
					  "counter insceased by %d\n",
					  slog_obj_name,
					  s->wrsSpllDelCnt - spll_DelCnt_prev);
		}
		if (s->wrsSpllMode == WRS_SPLL_MODE_SLAVE && s->wrsSpllDelCnt != spll_DelCnt_prev) {
			t->wrsSoftPLLStatus = WRS_SOFTPLL_STATUS_WARNING;
			snmp_log(LOG_WARNING, "SNMP: " SL_W " %s: "
					  "SoftPLL in Slave mode has unlocked. Delock "
					  "counter insceased by %d\n",
					  slog_obj_name,
					  s->wrsSpllDelCnt - spll_DelCnt_prev);
		}
	}
	/* check if any of fields equal to 0 or WARNING_NA */
	if (!t->wrsSoftPLLStatus) {
		if (s->wrsSpllMode == 0) {
			t->wrsSoftPLLStatus = WRS_SOFTPLL_STATUS_WARNING_NA;
			snmp_log(LOG_WARNING, "SNMP: " SL_NA " %s: "
					  "SoftPLL mode not set\n",
					  slog_obj_name);
		}
	}
	 /* check if OK */
	if ((!t->wrsSoftPLLStatus) && (
		((s->wrsSpllMode == WRS_SPLL_MODE_GRAND_MASTER && s->wrsSpllDelCnt == 0)
		    || (s->wrsSpllDelCnt == spll_DelCnt_prev))
		&& s->wrsSpllSeqState == WRS_SPLL_SEQ_STATE_READY
		&& ((s->wrsSpllMode == WRS_SPLL_MODE_GRAND_MASTER && s->wrsSpllAlignState == WRS_SPLL_ALIGN_STATE_LOCKED)
		    || (s->wrsSpllMode == WRS_SPLL_MODE_MASTER)
		    || (s->wrsSpllMode == WRS_SPLL_MODE_SLAVE)))
	) { /* OK */
		t->wrsSoftPLLStatus = WRS_SOFTPLL_STATUS_OK;
	}
	/* probably bug in previous conditions, this should never happen */
	if (!t->wrsSoftPLLStatus) {
		t->wrsSoftPLLStatus = WRS_SOFTPLL_STATUS_BUG;
		SLOG(SL_BUG);
	}

	spll_DelCnt_prev = s->wrsSpllDelCnt;
}

static void get_wrsSystemClockStatus(void){
	struct wrsCurrentTime_s *t=&wrsCurrentTime_s;
	int status=0;

	switch (t->wrsSystemClockStatusDetails) {
		case WRS_SYSTEM_CLOCK_STATUS_DETAILS_OK :
			status=WRS_SYSTEM_CLOCK_STATUS_OK;
			break;
		case WRS_SYSTEM_CLOCK_STATUS_DETAILS_IO_ERROR :
			status=WRS_SYSTEM_CLOCK_STATUS_ERROR_MINOR;
			break;
		case WRS_SYSTEM_CLOCK_STATUS_DETAILS_UNKNOWN  :
		case WRS_SYSTEM_CLOCK_STATUS_DETAILS_ERROR  :
		case WRS_SYSTEM_CLOCK_STATUS_DETAILS_NTP_ERROR  :
			status=WRS_SYSTEM_CLOCK_STATUS_ERROR;
			break;
		case WRS_SYSTEM_CLOCK_STATUS_DETAILS_THRESHOLD_EXCEEDED :
			status=WRS_SYSTEM_CLOCK_STATUS_THRESHOLD_EXCEEDED;
			break;
	}
	wrsTimingStatus_s.wrsSystemClockStatus = status;
}

static void get_wrsLeapSecondStatus(void){

	struct wrsCurrentTime_s *t=&wrsCurrentTime_s;
	struct wrsSpllStatus_s *s=&wrsSpllStatus_s;
	int status=0;

	switch (t->wrsLeapSecStatusDetails ) {
	case WRS_LEAP_SEC_STATUS_DETAILS_OK :
	case WRS_LEAP_SEC_STATUS_DETAILS_SEC_INSERTED:
	case WRS_LEAP_SEC_STATUS_DETAILS_SEC_DELETED:
		status=WRS_LEAP_SEC_STATUS_OK;
		break;
	case WRS_LEAP_SEC_STATUS_DETAILS_IO_ERROR:
		status=WRS_LEAP_SEC_STATUS_ERROR_MINOR;
		break;
	case WRS_LEAP_SEC_STATUS_DETAILS_UNKNOWN:
	case WRS_LEAP_SEC_STATUS_DETAILS_INTERNAL_ERROR:
	case WRS_LEAP_SEC_STATUS_DETAILS_TAI_READ_ERROR:
		status=WRS_LEAP_SEC_STATUS_ERROR;
		break;
	case WRS_LEAP_SEC_STATUS_DETAILS_FILE_EXPIRED:
		status = ( s->wrsSpllMode == WRS_SPLL_MODE_GRAND_MASTER
				|| s->wrsSpllMode == WRS_SPLL_MODE_MASTER)
			? WRS_LEAP_SEC_STATUS_WARNING : WRS_LEAP_SEC_STATUS_DETAILS_OK;
		break;
	}
	wrsTimingStatus_s.wrsLeapSecStatus = status;
}

static void get_wrsLeapSecondSourceStatus(void){

	struct wrsCurrentTime_s *t=&wrsCurrentTime_s;
	int status=0;

	switch (t->wrsLeapSecSourceStatusDetails ) {
	case WRS_LEAP_SEC_SRC_STATUS_DETAILS_OK :
	case WRS_LEAP_SEC_SRC_STATUS_DETAILS_UPDATED:
		status = WRS_LEAP_SEC_SRC_STATUS_OK;
		break;
	case WRS_LEAP_SEC_SRC_STATUS_DETAILS_IO_ERROR:
		status = WRS_LEAP_SEC_SRC_STATUS_ERROR_MINOR;
		break;
	case WRS_LEAP_SEC_SRC_STATUS_DETAILS_UNKNOWN:
	case WRS_LEAP_SEC_SRC_STATUS_DETAILS_DHCP_ERROR:
	case WRS_LEAP_SEC_SRC_STATUS_DETAILS_INVALID_URL:
	case WRS_LEAP_SEC_SRC_STATUS_DETAILS_INVALID_FILE:
	case WRS_LEAP_SEC_SRC_STATUS_DETAILS_DOWNLOAD_ERROR:
		status = WRS_LEAP_SEC_SRC_STATUS_ERROR;
		break;
	}
	wrsTimingStatus_s.wrsLeapSecSourceStatus = status;
}

static void get_wrsSlaveLinksStatus(unsigned int port_status_nrows)
{
	// struct wrsSpllStatus_s *s;
	struct wrsPortStatusTable_s *p_a;
	struct wrsPtpInstanceTable_s *i_a;
	struct wrsTimingStatus_s *t;
	// struct pp_instance *ppsi_i;
	int phys_port;
	int has_slave = 0;
	int i;

	/*********************************************************************\
	|************************ wrsSlaveLinksStatus ************************|
	\*********************************************************************/
	/*
	 * ok when every slave port is up when switch is in slave mode
	 * and when every slave port is down when switch in master/grandmaster
	 * mode. Don't care about non-wr, none and auto ports.
	*/
	// s = &wrsSpllStatus_s;
	p_a = wrsPortStatusTable_array;
	i_a = wrsPtpInstanceTable_array;
	t = &wrsTimingStatus_s;
	slog_obj_name = wrsSlaveLinksStatus_str;
	
	t->wrsSlaveLinksStatus = WRS_SLAVE_LINK_STATUS_OK;

	if (!shmem_ready_hald() || !shmem_ready_ppsi()) {
		/* HAL shmem not available yet */
		t->wrsSlaveLinksStatus = WRS_SLAVE_LINK_STATUS_WARNING_NA;
		return;
	}
	       
	for (i = 0; i < *ppsi_ppi_nlinks; i++) {
		// ppsi_i = ppsi_ppi + i;
		/* wrsSlaveLinksStatus is ERROR if any of the instances are ERROR */
		if (i_a[i].wrsPtpInstanceStatusError == WRS_SLAVE_LINK_STATUS_ERROR) {
			t->wrsSlaveLinksStatus = WRS_SLAVE_LINK_STATUS_ERROR;
			snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
				"One of the PTP Instances reports link status error. "
				"Check wrsPtpInstanceTable for more details.\n",
				slog_obj_name);
		}

		phys_port = i_a[i].wrsPtpInstancePort;
		if ((p_a[phys_port-1].wrsPortStatusLink == WRS_PORT_STATUS_LINK_UP) &&
		   (i_a[i].wrsPtpInstanceState == PPS_SLAVE))
		{
			has_slave = 1;
		}
	}
	
	/* wrsSlaveLinksStatus is ERROR if the configured mode is BC (defined by
	 * the clock class of defaultDS) and there is no active slave port*/
	if (ppsi_defaultDS->clockQuality.clockClass > 193 && has_slave == 0) {
		t->wrsSlaveLinksStatus = WRS_SLAVE_LINK_STATUS_ERROR;
		snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
			"In Boundary Clock mode, there is no active port in "
			"SLAVE state\n", slog_obj_name);
	}
}

static void get_wrsPTPFramesFlowing(unsigned int port_status_nrows)
{
	struct wrsPortStatusTable_s *p_a;
	struct wrsTimingStatus_s *t;
	int i;
	static int first_run = 1;

	/* store old values of TX and RX PTP counters to calculate delta */
	static unsigned long wrsPortStatusPtpTxFrames_prev[WRS_N_PORTS];
	static unsigned long wrsPortStatusPtpRxFrames_prev[WRS_N_PORTS];

	/*********************************************************************\
	|************************ wrsPTPFramesFlowing ************************|
	\*********************************************************************/
	/*
	 * Check if PTP frames are flowing. Check only on ports that are
	 * not (non-wr and none) and up.
	 */

	p_a = wrsPortStatusTable_array;
	t = &wrsTimingStatus_s;
	slog_obj_name = wrsPTPFramesFlowing_str;

	t->wrsPTPFramesFlowing = WRS_PTP_FRAMES_FLOWING_OK;
	for (i = 0; i < port_status_nrows; i++) {
		if (first_run == 1) {
			/* don't report errors during first run */
			t->wrsPTPFramesFlowing = WRS_PTP_FRAMES_FLOWING_FR;
			/* no need to check others */
			break;

		/* Error when there is no increase in TX/RX PTP counters.
		   Check only when port is not (non-wr and none) and port is down */
		}
		if ((p_a[i].wrsPortStatusMonitor != WRS_PORT_STATUS_MONITOR_DISABLE)
		    && (p_a[i].wrsPortStatusLink == WRS_PORT_STATUS_LINK_UP)) {
			if (wrsPortStatusPtpTxFrames_prev[i] == p_a[i].wrsPortStatusPtpTxFrames) {
				t->wrsPTPFramesFlowing = WRS_PTP_FRAMES_FLOWING_ERROR;
				snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
					 "No TX PTP frames flowing for port %i (wri%i) which is up and in WR mode\n",
					 slog_obj_name, i + 1, i + 1);
			}
			if (wrsPortStatusPtpRxFrames_prev[i] == p_a[i].wrsPortStatusPtpRxFrames) {
				t->wrsPTPFramesFlowing = WRS_PTP_FRAMES_FLOWING_ERROR;
				snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
					 "No RX PTP frames flowing for port %i (wri%i) which is up and in WR mode\n",
					 slog_obj_name, i + 1, i + 1);
			}
			/* can't go worse, but check other ports for logging */
		/* Warning N/A, skip when already error. Will not reach this
		 * point for first read */
		}
		if (p_a[i].wrsPortStatusLink == 0){
			/* assign if not error */
			if (t->wrsPTPFramesFlowing != WRS_PTP_FRAMES_FLOWING_ERROR) {
				t->wrsPTPFramesFlowing = WRS_PTP_FRAMES_FLOWING_WARNING_NA;
			}
			/* Log always for every port */
			snmp_log(LOG_WARNING, "SNMP: " SL_NA " %s: "
				  "Status of wrsPortStatusLink not available "
				  "for port %i (wri%i)\n",
				  slog_obj_name, i + 1, i + 1);
			/* continue with other ports, somewhere may be an
			 * error */
		}
	}

	for (i = 0; i < port_status_nrows; i++) {
		/* save current values */
		wrsPortStatusPtpTxFrames_prev[i] = p_a[i].wrsPortStatusPtpTxFrames;
		wrsPortStatusPtpRxFrames_prev[i] = p_a[i].wrsPortStatusPtpRxFrames;
	}

	first_run = 0;
}


#define GT_OID WRSTIMINGSTATUS_OID
#define GT_PICKINFO wrsTimingStatus_pickinfo
#define GT_DATA_FILL_FUNC wrsTimingStatus_data_fill
#define GT_DATA_STRUCT wrsTimingStatus_s
#define GT_GROUP_NAME "wrsTimingStatusGroup"
#define GT_INIT_FUNC init_wrsTimingStatusGroup

#include "wrsGroupTemplate.h"

#include "wrsSnmp.h"
#include "wrsOSStatusGroup.h"
#include "wrsTimingStatusGroup.h"
#include "wrsNetworkingStatusGroup.h"
#include "wrsGeneralStatusGroup.h"

static struct pickinfo wrsGeneralStatus_pickinfo[] = {
	FIELD(wrsGeneralStatus_s, ASN_INTEGER, wrsMainSystemStatus),
	FIELD(wrsGeneralStatus_s, ASN_INTEGER, wrsOSStatus),
	FIELD(wrsGeneralStatus_s, ASN_INTEGER, wrsTimingStatus),
	FIELD(wrsGeneralStatus_s, ASN_INTEGER, wrsNetworkingStatus),
};

struct wrsGeneralStatus_s wrsGeneralStatus_s;

time_t wrsGeneralStatus_data_fill(void)
{
	static time_t time_update; /* time of last update */
	time_t time_osstatus; /* time when wrsOSStatus data was updated */
	time_t time_timing_status; /* time when wrsTimingStatus data was
				    * updated */
	time_t time_networking_status; /* time when wrsNetworkingStatus data
					* was updated */
	struct wrsOSStatus_s *o;
	struct wrsTimingStatus_s *t;
	struct wrsNetworkingStatus_s *n;
	struct wrsGeneralStatus_s *g;

	time_osstatus = wrsOSStatus_data_fill();
	time_timing_status = wrsTimingStatus_data_fill();
	time_networking_status = wrsNetworkingStatus_data_fill();

	if (time_osstatus <= time_update
	    && time_timing_status <= time_update
	    && time_networking_status <= time_update) {
		/* cache not updated, return last update time */
		return time_update;
	}

	memset(&wrsGeneralStatus_s, 0, sizeof(wrsGeneralStatus_s));

	/*********************************************************************\
	|**************************** wrsOSStatus ****************************|
	\*********************************************************************/
	o = &wrsOSStatus_s;
	if ( /* check if error */
		o->wrsBootSuccessful == WRS_BOOT_SUCCESSFUL_ERROR
		|| o->wrsMemoryFreeLow == WRS_MEMORY_FREE_LOW_ERROR
		|| o->wrsCpuLoadHigh == WRS_CPU_LOAD_HIGH_ERROR
		|| o->wrsDiskSpaceLow == WRS_DISK_SPACE_LOW_ERROR
	) {
		wrsGeneralStatus_s.wrsOSStatus = WRS_OS_STATUS_ERROR;

	} else if ( /* check if warning */
		o->wrsBootSuccessful == WRS_BOOT_SUCCESSFUL_WARNING
		|| o->wrsTemperatureWarning == WRS_TEMPERATURE_WARNING_THOLD_NOT_SET
		|| o->wrsTemperatureWarning == WRS_TEMPERATURE_WARNING_TOO_HIGH
		|| o->wrsMemoryFreeLow == WRS_MEMORY_FREE_LOW_WARNING
		|| o->wrsCpuLoadHigh == WRS_CPU_LOAD_HIGH_WARNING
		|| o->wrsDiskSpaceLow == WRS_DISK_SPACE_LOW_WARNING
	) { /* warning */
		wrsGeneralStatus_s.wrsOSStatus = WRS_OS_STATUS_WARNING;

	} else if ( /* check if any of fields equal to 0 or WARNING_NA */
		o->wrsBootSuccessful == WRS_BOOT_SUCCESSFUL_WARNING_NA
		|| o->wrsBootSuccessful == 0
		|| o->wrsTemperatureWarning == 0
		|| o->wrsMemoryFreeLow == WRS_MEMORY_FREE_LOW_WARNING_NA
		|| o->wrsMemoryFreeLow == 0
		|| o->wrsCpuLoadHigh == 0
		|| o->wrsDiskSpaceLow == WRS_DISK_SPACE_LOW_WARNING_NA
		|| o->wrsDiskSpaceLow == 0
	) { /* warning NA */
		wrsGeneralStatus_s.wrsOSStatus = WRS_OS_STATUS_WARNING_NA;

	} else if ( /* check if OK */
		o->wrsBootSuccessful == WRS_BOOT_SUCCESSFUL_OK
		&& o->wrsTemperatureWarning == WRS_TEMPERATURE_WARNING_OK
		&& o->wrsMemoryFreeLow == WRS_MEMORY_FREE_LOW_OK
		&& o->wrsCpuLoadHigh == WRS_CPU_LOAD_HIGH_OK
		&& o->wrsDiskSpaceLow == WRS_DISK_SPACE_LOW_OK
	) { /* OK */
		wrsGeneralStatus_s.wrsOSStatus = WRS_OS_STATUS_OK;

	} else { /* probably bug in previous conditions,
		  * this should never happen */
		wrsGeneralStatus_s.wrsOSStatus = WRS_OS_STATUS_BUG;
	}

	/*********************************************************************\
	|************************** wrsTimingStatus **************************|
	\*********************************************************************/

	t = &wrsTimingStatus_s;
	if ( /* check if error */
		t->wrsPTPStatus == WRS_PTP_STATUS_ERROR
		|| t->wrsSoftPLLStatus == WRS_SOFTPLL_STATUS_ERROR
		|| t->wrsSlaveLinksStatus == WRS_SLAVE_LINK_STATUS_ERROR
		|| t->wrsPTPFramesFlowing == WRS_PTP_FRAMES_FLOWING_ERROR
		|| t->wrsSystemClockStatus == WRS_SYSTEM_CLOCK_STATUS_ERROR
	) {
		wrsGeneralStatus_s.wrsTimingStatus =
						WRS_TIMING_STATUS_ERROR;

	} else if ( /* check if warning */
		   t->wrsSoftPLLStatus == WRS_SOFTPLL_STATUS_WARNING
		|| t->wrsSystemClockStatus == WRS_SYSTEM_CLOCK_STATUS_WARNING
	) { /* warning */
		wrsGeneralStatus_s.wrsTimingStatus =
						WRS_TIMING_STATUS_WARNING;

	} else if ( /* check if any of fields equal to 0 or WARNING_NA */
		t->wrsPTPStatus == 0
		|| t->wrsSoftPLLStatus == WRS_SOFTPLL_STATUS_WARNING_NA
		|| t->wrsSoftPLLStatus == 0
		|| t->wrsSlaveLinksStatus == 0
		|| t->wrsSlaveLinksStatus == WRS_SLAVE_LINK_STATUS_WARNING_NA
		|| t->wrsPTPFramesFlowing == 0
		|| t->wrsPTPFramesFlowing == WRS_PTP_FRAMES_FLOWING_WARNING_NA
		|| t->wrsSystemClockStatus == 0
		|| t->wrsSystemClockStatus == WRS_SYSTEM_CLOCK_STATUS_WARNING_NA
	) { /* warning NA */
		wrsGeneralStatus_s.wrsTimingStatus =
					      WRS_TIMING_STATUS_WARNING_NA;

	} else if ( /* check if OK, FR (first read) is also ok */
		(t->wrsPTPStatus == WRS_PTP_STATUS_OK
		    || t->wrsPTPStatus == WRS_PTP_STATUS_FR) /* FR*/
		&& t->wrsSoftPLLStatus == WRS_SOFTPLL_STATUS_OK
		&& t->wrsSlaveLinksStatus == WRS_SLAVE_LINK_STATUS_OK
		&& (t->wrsPTPFramesFlowing == WRS_PTP_FRAMES_FLOWING_OK
		    || t->wrsPTPFramesFlowing == WRS_PTP_FRAMES_FLOWING_FR) /* FR */
		&& t->wrsSystemClockStatus == WRS_SYSTEM_CLOCK_STATUS_OK

	) { /* OK */
		wrsGeneralStatus_s.wrsTimingStatus =
						WRS_TIMING_STATUS_OK;

	} else { /* probably bug in previous conditions,
		  * this should never happen */
		wrsGeneralStatus_s.wrsTimingStatus =
						WRS_TIMING_STATUS_BUG;
	}

	/*********************************************************************\
	|************************ wrsNetworkingStatus ************************|
	\*********************************************************************/
	n = &wrsNetworkingStatus_s;
	if ( /* check if error */
		n->wrsSFPsStatus == WRS_SFPS_STATUS_ERROR
		|| n->wrsEndpointStatus == WRS_ENDPOINT_STATUS_ERROR
		|| n->wrsSwcoreStatus == WRS_SWCORE_STATUS_ERROR
		|| n->wrsRTUStatus == WRS_RTU_STATUS_ERROR
	) {
		wrsGeneralStatus_s.wrsNetworkingStatus =
						WRS_NETWORKING_STATUS_ERROR;

	} else if ( /* check if warning */
		n->wrsSFPsStatus == WRS_SFPS_STATUS_WARNING
	) { /* warning */
		wrsGeneralStatus_s.wrsNetworkingStatus =
						WRS_NETWORKING_STATUS_WARNING;

	} else if ( /* check if any of fields equal to 0 or WARNING_NA */
		n->wrsSFPsStatus == WRS_SFPS_STATUS_WARNING_NA
		|| n->wrsSFPsStatus == 0
		|| n->wrsEndpointStatus == 0
		|| n->wrsSwcoreStatus == 0
		|| n->wrsRTUStatus == 0
	) { /* warning NA */
		wrsGeneralStatus_s.wrsNetworkingStatus =
					      WRS_NETWORKING_STATUS_WARNING_NA;

	} else if ( /* check if OK, FR (first read) is also ok */
		n->wrsSFPsStatus == WRS_SFPS_STATUS_OK
		&& (n->wrsEndpointStatus == WRS_ENDPOINT_STATUS_OK
		    || n->wrsEndpointStatus == WRS_ENDPOINT_STATUS_FR) /* FR*/
		&& (n->wrsSwcoreStatus == WRS_SWCORE_STATUS_OK
		    || n->wrsSwcoreStatus == WRS_SWCORE_STATUS_FR) /* FR */
		&& (n->wrsRTUStatus == WRS_RTU_STATUS_OK
		    || n->wrsRTUStatus == WRS_RTU_STATUS_FR) /* FR */
	) { /* OK */
		wrsGeneralStatus_s.wrsNetworkingStatus =
						WRS_NETWORKING_STATUS_OK;

	} else { /* probably bug in previous conditions,
		  * this should never happen */
		wrsGeneralStatus_s.wrsNetworkingStatus =
						WRS_NETWORKING_STATUS_BUG;
	}

	/*********************************************************************\
	|************************ wrsMainSystemStatus ************************|
	\*********************************************************************/

	/* update at the end of this group to have latest results of other
	 * statuses */
	g = &wrsGeneralStatus_s;
	if ( /* check if error */
		g->wrsOSStatus == WRS_OS_STATUS_ERROR
		|| g->wrsTimingStatus == WRS_TIMING_STATUS_ERROR
		|| g->wrsNetworkingStatus == WRS_NETWORKING_STATUS_ERROR
	) {
		wrsGeneralStatus_s.wrsMainSystemStatus =
						WRS_MAIN_SYSTEM_STATUS_ERROR;

	} else if ( /* check if warning */
		g->wrsOSStatus == WRS_OS_STATUS_WARNING
		|| g->wrsTimingStatus == WRS_TIMING_STATUS_WARNING
		|| g->wrsNetworkingStatus == WRS_NETWORKING_STATUS_WARNING
	) { /* warning */
		wrsGeneralStatus_s.wrsMainSystemStatus =
						WRS_MAIN_SYSTEM_STATUS_WARNING;

	} else if ( /* check if any of fields equal to 0 or WARNING_NA */
		g->wrsOSStatus == WRS_OS_STATUS_WARNING_NA
		|| g->wrsOSStatus == 0
		|| g->wrsTimingStatus == WRS_TIMING_STATUS_WARNING_NA
		|| g->wrsTimingStatus == 0
		|| g->wrsNetworkingStatus == WRS_NETWORKING_STATUS_WARNING_NA
		|| g->wrsNetworkingStatus == 0
	) { /* warning NA */
		wrsGeneralStatus_s.wrsMainSystemStatus =
					WRS_MAIN_SYSTEM_STATUS_WARNING_NA;

	} else if ( /* check if OK */
		g->wrsOSStatus == WRS_OS_STATUS_OK
		&& g->wrsTimingStatus == WRS_TIMING_STATUS_OK
		&& g->wrsNetworkingStatus == WRS_NETWORKING_STATUS_OK
	) { /* OK */
		wrsGeneralStatus_s.wrsMainSystemStatus =
						WRS_MAIN_SYSTEM_STATUS_OK;

	} else { /* probably bug in previous conditions,
		  * this should never happen */
		wrsGeneralStatus_s.wrsMainSystemStatus =
						WRS_MAIN_SYSTEM_STATUS_BUG;
	}

	time_update = get_monotonic_sec();

	/* there was an update, return current time */
	return time_update;
}

#define GT_OID WRSGENERALSTATUS_OID
#define GT_PICKINFO wrsGeneralStatus_pickinfo
#define GT_DATA_FILL_FUNC wrsGeneralStatus_data_fill
#define GT_DATA_STRUCT wrsGeneralStatus_s
#define GT_GROUP_NAME "wrsGeneralStatusGroup"
#define GT_INIT_FUNC init_wrsGeneralStatusGroup

#include "wrsGroupTemplate.h"

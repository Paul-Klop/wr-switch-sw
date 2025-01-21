#ifndef WRS_NETWORKING_STATUS_GROUP_H
#define WRS_NETWORKING_STATUS_GROUP_H

#define WRSNETWORKINGSTATUS_OID WRS_OID, 6, 2, 3

#define TX_FORWARD_DELTA 10

#define WRS_SFPS_STATUS_OK 1			/* ok */
#define WRS_SFPS_STATUS_ERROR 2			/* error */
#define WRS_SFPS_STATUS_WARNING 3		/* warning, not used */
#define WRS_SFPS_STATUS_WARNING_NA 4	/* warning, at least one field is
					 * equal to 0 (NA),shouldn't happen in
					 * normal operation */
#define WRS_SFPS_STATUS_BUG 5			/* warning */

#define WRS_ENDPOINT_STATUS_OK 1		/* ok */
#define WRS_ENDPOINT_STATUS_ERROR 2		/* error */
#define WRS_ENDPOINT_STATUS_FR 6		/* ok, first read */

#define WRS_SWCORE_STATUS_OK 1			/* ok */
#define WRS_SWCORE_STATUS_ERROR 2		/* error */
#define WRS_SWCORE_STATUS_FR 6			/* ok, first read */

#define WRS_RTU_STATUS_OK 1			/* ok */
#define WRS_RTU_STATUS_ERROR 2			/* error */
#define WRS_RTU_STATUS_FR 6			/* ok, first read */

struct wrsNetworkingStatus_s {
	int wrsSFPsStatus;
	int wrsEndpointStatus;
	int wrsSwcoreStatus;
	int wrsRTUStatus;
};

extern struct wrsNetworkingStatus_s wrsNetworkingStatus_s;
time_t wrsNetworkingStatus_data_fill(void);
void init_wrsNetworkingStatusGroup(void);

struct ns_pstats {
	/* wrsEndpointStatus */
	uint64_t wrsPstatsHCTXUnderrun;		/* 1 */
	uint64_t wrsPstatsHCRXOverrun;		/* 2 */
	uint64_t wrsPstatsHCRXInvalidCode;	/* 3 */
	uint64_t wrsPstatsHCRXSyncLost;		/* 4 */
	uint64_t wrsPstatsHCRXPfilterDropped;	/* 6 */
	uint64_t wrsPstatsHCRXPCSErrors;	/* 7 */
	uint64_t wrsPstatsHCRXCRCErrors;	/* 10 */
	/* wrsSwcoreStatus */
	/* Problem with the SwCore or Endpoint HDL module */
	uint64_t wrsPstatsHCTXFrames; 		/* 19 */
	uint64_t wrsPstatsHCForwarded;		/* 38 */
	uint64_t wrsPstatsHCNICTXFrames;	/* 40 */
	/* Too much HP traffic / Per-priority queue full */
	uint64_t wrsPstatsHCRXFrames;		/* 20 */
	uint64_t wrsPstatsHCRXPrio0;		/* 22 */
	uint64_t wrsPstatsHCRXPrio1;		/* 23 */
	uint64_t wrsPstatsHCRXPrio2;		/* 24 */
	uint64_t wrsPstatsHCRXPrio3;		/* 25 */
	uint64_t wrsPstatsHCRXPrio4;		/* 26 */
	uint64_t wrsPstatsHCRXPrio5;		/* 27 */
	uint64_t wrsPstatsHCRXPrio6;		/* 28 */
	uint64_t wrsPstatsHCRXPrio7;		/* 29 */
	uint64_t wrsPstatsHCFastMatchPriority;	/* 33 */
	/* wrsRTUStatus */
	uint64_t wrsPstatsHCRXDropRTUFull;	/* 21 */
};

/* parameters read from dot-config */
struct wrsNetworkingStatus_config {
	int disable_wrsSwcoreStatus;
	float hp_frame_rate;
	float rx_frame_rate;
	float rx_prio_frame_rate;
	uint64_t tx_forward_delta;
};

#endif /* WRS_NETWORKING_STATUS_GROUP_H */

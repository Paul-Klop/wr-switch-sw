#ifndef WRS_PTP_DATA_TABLE_H
#define WRS_PTP_DATA_TABLE_H

#define WRSPTPDATATABLE_CACHE_TIMEOUT 5
#define WRSPTPDATATABLE_OID WRS_OID, 7, 5
/* Right now we allow only one servo instance, it will change in the future
 * when switchover is implemented */
#define WRS_MAX_N_SERVO_INSTANCES 1

#define PTP_SERVO_STATE_N_UNINTIALIZED         0
#define PTP_SERVO_STATE_N_SYNC_NSEC            1
#define PTP_SERVO_STATE_N_SYNC_SEC             2
#define PTP_SERVO_STATE_N_SYNC_PHASE    	   3
#define PTP_SERVO_STATE_N_TRACK_PHASE   	   4
#define PTP_SERVO_STATE_N_WAIT_OFFSET_STABLE   5
#define PTP_SERVO_STATE_N_STANDARD_PTP	       99

struct wrsPtpDataTable_s {
	uint32_t wrsPtpDataIndex;		/* not reported, index fields has to be marked
				 * as not-accessible in MIB */
	char wrsPtpPortName[12];	/* port name on which ptp servo instance in
				 * running FIXME: not implemented */
	ClockIdentity wrsPtpGrandmasterID;	/* FIXME: not implemented */
	ClockIdentity wrsPtpOwnID;	/* FIXME: not implemented */
	int wrsPtpMode_obsolete;		/* Obsolete */
	char wrsPtpServoState[32]; /* State as string */
	int wrsPtpServoStateN;	/* state number */
	int wrsPtpPhaseTracking;
	char wrsPtpSyncSource[32];	/* FIXME: not implemented */
	int64_t wrsPtpClockOffsetPs;
	int32_t wrsPtpClockOffsetPsHR;	/* Human readable version of clock_offset,
				 * saturated to int limits */
	int32_t wrsPtpSkew;
	int64_t wrsPtpRTT;
	uint32_t wrsPtpLinkLength;
	uint32_t wrsPtpServoUpdates;
	int32_t wrsPtpDeltaTxM;
	int32_t wrsPtpDeltaRxM;
	int32_t wrsPtpDeltaTxS;
	int32_t wrsPtpDeltaRxS;
	uint32_t wrsPtpServoStateErrCnt;
	uint32_t wrsPtpClockOffsetErrCnt;
	uint32_t wrsPtpRTTErrCnt;
	uint64_t wrsPtpServoUpdateTime;
	int wrsPtpServoExt;
	int32_t wrsPtpServoMeanDelay;
	int32_t wrsPtpServoDelayMS;
	int32_t wrsPtpServoDelayMM;
	int32_t wrsPtpDelayAsymmetryPS;
	int32_t wrsPtpDelayCoefficientScaledH;
	uint32_t wrsPtpDelayCoefficientScaledL;
	char wrsPtpDelayCoefficientStr[64];
	int32_t wrsPtpIngressLatency;
	int32_t wrsPtpEgressLatency;
};

extern struct wrsPtpDataTable_s wrsPtpDataTable_array[WRS_MAX_N_SERVO_INSTANCES];

time_t wrsPtpDataTable_data_fill(unsigned int *rows);
void init_wrsPtpDataTable(void);

#endif /* WRS_PTP_DATA_TABLE_H */

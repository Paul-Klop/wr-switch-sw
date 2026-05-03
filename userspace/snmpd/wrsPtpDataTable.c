#include "wrsSnmp.h"
#include "snmp_shmem.h"
#include "wrsPtpDataTable.h"
#include "../proto-ext-common/wrh-servo_state_name.h"

struct wrsPtpDataTable_s wrsPtpDataTable_array[WRS_MAX_N_SERVO_INSTANCES];

/* Save pointers to the types of some OIDs.
 * If a type of these OIDs is set to SNMP_NOSUCHINSTANCE, then an OID is not
 * returned during a query. */
static int *wrsPtpRTT_type_p = NULL;
static int *wrsPtpDeltaTxM_type_p = NULL;
static int *wrsPtpDeltaRxM_type_p = NULL;
static int *wrsPtpDeltaTxS_type_p = NULL;
static int *wrsPtpDeltaRxS_type_p = NULL;

#define WRSPTPRTT_ASN_TYPE ASN_COUNTER64
#define WRSPTPDELTA_ASN_TYPE ASN_INTEGER

static struct pickinfo wrsPtpDataTable_pickinfo[] = {
	/* Warning: strings are a special case for snmp format */
	FIELD(wrsPtpDataTable_s, ASN_UNSIGNED, wrsPtpDataIndex), /* not reported */
	FIELD(wrsPtpDataTable_s, ASN_OCTET_STR, wrsPtpPortName),
	FIELD(wrsPtpDataTable_s, ASN_OCTET_STR, wrsPtpGrandmasterID),
	FIELD(wrsPtpDataTable_s, ASN_OCTET_STR, wrsPtpOwnID),
	FIELD(wrsPtpDataTable_s, ASN_INTEGER, wrsPtpMode_obsolete),/* obsolete */
	FIELD(wrsPtpDataTable_s, ASN_OCTET_STR, wrsPtpServoState),
	FIELD(wrsPtpDataTable_s, ASN_INTEGER, wrsPtpServoStateN),
	FIELD(wrsPtpDataTable_s, ASN_INTEGER, wrsPtpPhaseTracking),
	FIELD(wrsPtpDataTable_s, ASN_OCTET_STR, wrsPtpSyncSource),
	FIELD(wrsPtpDataTable_s, ASN_COUNTER64, wrsPtpClockOffsetPs),
	FIELD(wrsPtpDataTable_s, ASN_INTEGER, wrsPtpClockOffsetPsHR),
	FIELD(wrsPtpDataTable_s, ASN_INTEGER, wrsPtpSkew),
	FIELD(wrsPtpDataTable_s, WRSPTPRTT_ASN_TYPE, wrsPtpRTT),
	FIELD(wrsPtpDataTable_s, ASN_UNSIGNED, wrsPtpLinkLength),
	FIELD(wrsPtpDataTable_s, ASN_COUNTER, wrsPtpServoUpdates),
	FIELD(wrsPtpDataTable_s, WRSPTPDELTA_ASN_TYPE, wrsPtpDeltaTxM),
	FIELD(wrsPtpDataTable_s, WRSPTPDELTA_ASN_TYPE, wrsPtpDeltaRxM),
	FIELD(wrsPtpDataTable_s, WRSPTPDELTA_ASN_TYPE, wrsPtpDeltaTxS),
	FIELD(wrsPtpDataTable_s, WRSPTPDELTA_ASN_TYPE, wrsPtpDeltaRxS),
	FIELD(wrsPtpDataTable_s, ASN_COUNTER, wrsPtpServoStateErrCnt),
	FIELD(wrsPtpDataTable_s, ASN_COUNTER, wrsPtpClockOffsetErrCnt),
	FIELD(wrsPtpDataTable_s, ASN_COUNTER, wrsPtpRTTErrCnt),
	FIELD(wrsPtpDataTable_s, ASN_COUNTER64, wrsPtpServoUpdateTime),
	FIELD(wrsPtpDataTable_s, ASN_INTEGER, wrsPtpServoExt),
	FIELD(wrsPtpDataTable_s, ASN_INTEGER, wrsPtpServoMeanDelay),
	FIELD(wrsPtpDataTable_s, ASN_INTEGER, wrsPtpServoDelayMS),
	FIELD(wrsPtpDataTable_s, ASN_INTEGER, wrsPtpServoDelayMM),
	FIELD(wrsPtpDataTable_s, ASN_INTEGER, wrsPtpDelayAsymmetryPS),
	FIELD(wrsPtpDataTable_s, ASN_INTEGER, wrsPtpDelayCoefficientScaledH),
	FIELD(wrsPtpDataTable_s, ASN_UNSIGNED, wrsPtpDelayCoefficientScaledL),
	FIELD(wrsPtpDataTable_s, ASN_OCTET_STR, wrsPtpDelayCoefficientStr),
	FIELD(wrsPtpDataTable_s, ASN_INTEGER, wrsPtpIngressLatency),
	FIELD(wrsPtpDataTable_s, ASN_INTEGER, wrsPtpEgressLatency),
	FIELD(wrsPtpDataTable_s, ASN_UNSIGNED, wrsPtpClockClass),
	FIELD(wrsPtpDataTable_s, ASN_UNSIGNED, wrsPtpStepsRemoved),
};

static char *relativeDifferenceToString(RelativeDifference time, char *buf)
{
	char sign;
	int32_t nsecs;
	uint64_t sub_yocto = 0;
	int64_t fraction;
	uint64_t bitWeight = 500000000000000000;
	uint64_t mask;

	if (time < 0) {
		time =- time;
		sign ='-';
	} else {
		sign ='+';
	}

	nsecs=time >> REL_DIFF_FRACBITS;
	fraction = time & REL_DIFF_FRACMASK;
	for (mask = (uint64_t) 1 << (REL_DIFF_FRACBITS - 1); mask != 0; mask >>= 1) {
		if (mask & fraction)
			sub_yocto += bitWeight;
		bitWeight /= 2;
	}

	sprintf(buf, "%c%" PRId32 ".%018" PRIu64, sign, nsecs, sub_yocto);
	return buf;
}

time_t wrsPtpDataTable_data_fill(unsigned int *n_rows)
{
	unsigned ii;
	unsigned retries = 0;
	static time_t time_update;
	time_t time_cur;
	static int n_rows_local = 0;
	int si = 0;
	int i;
	struct wrsPtpDataTable_s *ptp_a;
	struct pp_instance *ppsi_i;
	struct pp_servo *ppsi_servo;
	struct wr_data *wr_d = NULL;
	struct l1e_data *l1e_d = NULL;
	struct wr_servo_ext *wr_servo = NULL;
	struct wrh_servo_t *wrh_servo = NULL;
	portDS_t *portDS;
	char *tmp_name;
	static int servoStateMapping[]={
			[WRH_UNINITIALIZED]= PTP_SERVO_STATE_N_UNINTIALIZED,
			[WRH_SYNC_NSEC]= PTP_SERVO_STATE_N_SYNC_NSEC,
			[WRH_SYNC_TAI]= PTP_SERVO_STATE_N_SYNC_SEC,
			[WRH_SYNC_PHASE]= PTP_SERVO_STATE_N_SYNC_PHASE,
			[WRH_TRACK_PHASE]= PTP_SERVO_STATE_N_TRACK_PHASE,
			[WRH_WAIT_OFFSET_STABLE]= PTP_SERVO_STATE_N_WAIT_OFFSET_STABLE,
	};

	/* number of rows does not change for wrsPortStatusTable */
	if (n_rows)
		*n_rows = n_rows_local;

	time_cur = get_monotonic_sec();
	if (time_update
	    && time_cur - time_update < WRSPTPDATATABLE_CACHE_TIMEOUT) {
		/* cache not updated, return last update time */
		return time_update;
	}
	time_update = time_cur;

	memset(&wrsPtpDataTable_array, 0, sizeof(wrsPtpDataTable_array));

	/* check whether shmem is available */
	if (!shmem_ready_ppsi()) {
		snmp_log(LOG_ERR, "SNMP: " SL_ER
		" %s: Unable to read PPSI's shmem\n",
			__func__);
		/* Keep one empty instance. If set to 0 all PPSI related OIDs
		 * disappear */
		n_rows_local = 1;
		return time_update;
	} else {
		n_rows_local = WRS_MAX_N_SERVO_INSTANCES;
	}

	if (n_rows)
		*n_rows = n_rows_local;

	ptp_a = wrsPtpDataTable_array;

	/* servo/slave instance counter */
	si = 0;

	/* assume that there is only one servo, will change when switchover is
	 * implemented */
	while (1) {
		ii = wrs_shm_seqbegin(ppsi_head);

		/* Global PTP quality readout; keep row 0 valid even when
		 * no slave/servo instance is currently present. */
		ptp_a[0].wrsPtpClockClass =
		        ppsi_parentDS->grandmasterClockQuality.clockClass;
		ptp_a[0].wrsPtpStepsRemoved =
		        ppsi_currentDS->stepsRemoved;

		for (i = 0; i < *ppsi_ppi_nlinks; i++)
		{
			/* report not more than max number of servo instances */
			if( si >= WRS_MAX_N_SERVO_INSTANCES)
				break;

			ppsi_i = ppsi_ppi + i;
			if (ppsi_i->state == PPS_SLAVE)
			{

				/*********** from ppsi instance ***************/

				/* wrsPtpPortName */
				tmp_name = (char *) wrs_shm_follow(ppsi_head,
				ppsi_i->iface_name);
				strncpy(ptp_a[si].wrsPtpPortName, tmp_name, 12);
				ptp_a[si].wrsPtpPortName[11] = '\0';

				/*********** from standard servo ***************/

				/* get servo for ptp instance in Slave state*/
				ppsi_servo = wrs_shm_follow(ppsi_head,
				ppsi_i->servo);

				/* wrsPtpGrandmasterID */
				memcpy(&ptp_a[si].wrsPtpGrandmasterID,
					&ppsi_parentDS->grandmasterIdentity,
					sizeof(ClockIdentity));

				/* wrsPtpOwnID */
				memcpy(&ptp_a[si].wrsPtpOwnID,
					&ppsi_defaultDS->clockIdentity,
					sizeof(ClockIdentity));

				/* wrsPtpServoState */
				strcpy(ptp_a[si].wrsPtpServoState,
				       wrh_servo_state_name[ppsi_servo->state]);

				/* wrsPtpServoStateN */
				if ( ppsi_i->extState == PP_EXSTATE_DISABLE
						|| ppsi_i->extState == PP_EXSTATE_PTP ) {
					ptp_a[si].wrsPtpServoStateN= PTP_SERVO_STATE_N_STANDARD_PTP;
				} else {
					if ( ppsi_servo->state>=0 && ppsi_servo->state < ARRAY_SIZE(servoStateMapping) ) {
						ptp_a[si].wrsPtpServoStateN=servoStateMapping[ppsi_servo->state];
					} else {
						ptp_a[si].wrsPtpServoStateN=ppsi_servo->state;
					}
				}

				/* wrsPtpClockOffsetPs */
				ptp_a[si].wrsPtpClockOffsetPs = pp_time_to_picos(&ppsi_servo->offsetFromMaster);
				if ( ptp_a[si].wrsPtpClockOffsetPs<0)
					ptp_a[si].wrsPtpClockOffsetPs*=-1;

				/* wrsPtpClockOffsetPsHR */
				ptp_a[si].wrsPtpClockOffsetPsHR =
				int_saturate(ptp_a[si].wrsPtpClockOffsetPs);

				/* wrsPtpServoUpdates */
				ptp_a[si].wrsPtpServoUpdates =
				ppsi_servo->update_count;

				/* wrsPtpServoUpdateTime */
				ptp_a[si].wrsPtpServoUpdateTime = 
 				ppsi_servo->update_time.secs * 1000 * 1000 * 1000
				+ (ppsi_servo->update_time.scaled_nsecs >> 16);

				/* wrsPtpServoExt */
				ptp_a[si].wrsPtpServoExt = 1+
				ppsi_i->protocol_extension;

				/******** from extensions-specific ************/
				if (ppsi_i->protocol_extension == PPSI_EXT_WR
				    || ppsi_i->protocol_extension == PPSI_EXT_L1S) {
					if (ppsi_i->protocol_extension == PPSI_EXT_WR) {
						wr_d       = (struct wr_data *)
								wrs_shm_follow(ppsi_head,
								ppsi_i->ext_data);
						wr_servo   = &wr_d->servo_ext;
						wrh_servo  = &wr_d->servo;
					}
					if (ppsi_i->protocol_extension == PPSI_EXT_L1S) {
						l1e_d       = (struct l1e_data *)
								wrs_shm_follow(ppsi_head,
								ppsi_i->ext_data);
						wr_servo   = NULL;
						wrh_servo  = &l1e_d->servo;
					}

					/* wrsPtpLinkLength */
					/* crtt / 2 * c / ri
					c = 299792458 - speed of light in m/s
					ri = 1.4682 - refractive index for fiber g.652. However,
						    experimental measurements using long (~5km) and
						    short (few m) fibers gave a value 1.4688.
						    For different wavelengths this value will be different
						    Please note that this value is just an estimation.
					*/
					ptp_a[si].wrsPtpLinkLength = wrh_servo->delayMM_ps / 2 / 1e6 * 299.792458 / 1.4688;

					/* wrsPtpPhaseTracking */
					ptp_a[si].wrsPtpPhaseTracking =
					1 + wrh_servo->tracking_enabled;

					/* wrsPtpSyncSource */
					tmp_name = (char *) wrs_shm_follow(ppsi_head, ppsi_i->iface_name);
					strncpy(ptp_a[si].wrsPtpSyncSource, tmp_name, 12);
					ptp_a[si].wrsPtpSyncSource[31] = '\0';

					/* wrsPtpSkew */
					ptp_a[si].wrsPtpSkew =
					int_saturate(wrh_servo->skew_ps);

					if (wr_servo) {
						/* wrsPtpDeltaTxM */
						ptp_a[si].wrsPtpDeltaTxM =
							pp_time_to_picos(&wr_servo->delta_txm);

						/* wrsPtpDeltaRxM */
						ptp_a[si].wrsPtpDeltaRxM =
							pp_time_to_picos(&wr_servo->delta_rxm);

						/* wrsPtpDeltaTxS */
						ptp_a[si].wrsPtpDeltaTxS =
							pp_time_to_picos(&wr_servo->delta_txs);

						/* wrsPtpDeltaRxS */
						ptp_a[si].wrsPtpDeltaRxS =
							pp_time_to_picos(&wr_servo->delta_rxs);

						*wrsPtpDeltaTxM_type_p = WRSPTPDELTA_ASN_TYPE;
						*wrsPtpDeltaRxM_type_p = WRSPTPDELTA_ASN_TYPE;
						*wrsPtpDeltaTxS_type_p = WRSPTPDELTA_ASN_TYPE;
						*wrsPtpDeltaRxS_type_p = WRSPTPDELTA_ASN_TYPE;
					} else {
						/* Deltas are not available */
						*wrsPtpDeltaTxM_type_p = SNMP_NOSUCHINSTANCE;
						*wrsPtpDeltaRxM_type_p = SNMP_NOSUCHINSTANCE;
						*wrsPtpDeltaTxS_type_p = SNMP_NOSUCHINSTANCE;
						*wrsPtpDeltaRxS_type_p = SNMP_NOSUCHINSTANCE;
					}

					/* wrsPtpServoStateErrCnt */
					ptp_a[si].wrsPtpServoStateErrCnt =
					wrh_servo->n_err_state;

					/* wrsPtpClockOffsetErrCnt */
					ptp_a[si].wrsPtpClockOffsetErrCnt =
                                	wrh_servo->n_err_offset;

					/* wrsPtpRTTErrCnt */
					ptp_a[si].wrsPtpRTTErrCnt =
					wrh_servo->n_err_delta_rtt;

					/* wrsPtpRTT */
					if (wr_servo) {
						*wrsPtpRTT_type_p = WRSPTPRTT_ASN_TYPE;
						ptp_a[si].wrsPtpRTT = pp_time_to_picos(&wr_servo->rawDelayMM);
					} else {
						/* wrsPtpRTT is not available */
						*wrsPtpRTT_type_p = SNMP_NOSUCHINSTANCE;
					}

					/* wrsPtpServoMeanDelay */
					ptp_a[si].wrsPtpServoMeanDelay = int_saturate(pp_time_to_picos(&ppsi_servo->meanDelay));

					/* wrsPtpServoDelayMS */
					ptp_a[si].wrsPtpServoDelayMS = int_saturate(pp_time_to_picos(&ppsi_servo->delayMS));

					/* wrsPtpServoDelayMM */
					ptp_a[si].wrsPtpServoDelayMM = int_saturate(pp_time_to_picos(&ppsi_servo->delayMM));

					/* wrsPtpDelayAsymmetryPS */
					if ((portDS = wrs_shm_follow(ppsi_head, ppsi_i->portDS)))
						ptp_a[si].wrsPtpDelayAsymmetryPS = int_saturate(interval_to_picos(portDS->delayAsymmetry));

					/* wrsPtpDelayCoefficientScaledH */
					ptp_a[si].wrsPtpDelayCoefficientScaledH = ppsi_i->asymmetryCorrectionPortDS.scaledDelayCoefficient >> 32;

					/* wrsPtpDelayCoefficientScaledL */
					ptp_a[si].wrsPtpDelayCoefficientScaledL = ppsi_i->asymmetryCorrectionPortDS.scaledDelayCoefficient & 0xFFFFFFFF;

					/* wrsPtpDelayCoefficientStr */
					relativeDifferenceToString(ppsi_i->asymmetryCorrectionPortDS.scaledDelayCoefficient, ptp_a[si].wrsPtpDelayCoefficientStr);

					/* wrsPtpIngressLatency */
					ptp_a[si].wrsPtpIngressLatency = int_saturate(interval_to_picos(ppsi_i->timestampCorrectionPortDS.ingressLatency));

					/* wrsPtpEgressLatency */
					ptp_a[si].wrsPtpEgressLatency = int_saturate(interval_to_picos(ppsi_i->timestampCorrectionPortDS.egressLatency));
				} else {
					memset(ptp_a[si].wrsPtpSyncSource,
					0, 32 * sizeof(char));

					ptp_a[si].wrsPtpPhaseTracking     = 0;
					ptp_a[si].wrsPtpSkew              = 0;
					ptp_a[si].wrsPtpDeltaTxM          = 0;
					ptp_a[si].wrsPtpDeltaRxM          = 0;
					ptp_a[si].wrsPtpDeltaTxS          = 0;
					ptp_a[si].wrsPtpDeltaRxS          = 0;
					ptp_a[si].wrsPtpServoStateErrCnt  = 0;
					ptp_a[si].wrsPtpClockOffsetErrCnt = 0;
					ptp_a[si].wrsPtpRTTErrCnt         = 0;

					/* wrsPtpRTT */
					ptp_a[si].wrsPtpRTT = 2 * pp_time_to_picos(&ppsi_servo->meanDelay);
				}
				/* look for next PTP Instance in Slave state*/
				si++;
			}
		}

		retries++;
		if (retries > 100) {
			snmp_log(LOG_ERR,  "SNMP: " SL_ER
				 "%s: too many retries to read PPSI\n",
				 __func__);
			retries = 0;
			}
		if (!wrs_shm_seqretry(ppsi_head, ii))
			break; /* consistent read */
		usleep(1000);
	}
	/* there was an update, return current time */
	return time_update;
}

static void post_init_wrsPtpDataTable(void)
{
	int i;
	struct pickinfo *p_pickinfo = wrsPtpDataTable_pickinfo;

	for (i = 0; i < ARRAY_SIZE(wrsPtpDataTable_pickinfo); i++, p_pickinfo++) {
		/* Save pointers to OIDs' type fields. */
		if (p_pickinfo->offset == offsetof(struct wrsPtpDataTable_s, wrsPtpRTT))
			wrsPtpRTT_type_p = &p_pickinfo->type;
		if (p_pickinfo->offset == offsetof(struct wrsPtpDataTable_s, wrsPtpDeltaTxM))
			wrsPtpDeltaTxM_type_p = &p_pickinfo->type;
		if (p_pickinfo->offset == offsetof(struct wrsPtpDataTable_s, wrsPtpDeltaRxM))
			wrsPtpDeltaRxM_type_p = &p_pickinfo->type;
		if (p_pickinfo->offset == offsetof(struct wrsPtpDataTable_s, wrsPtpDeltaTxS))
			wrsPtpDeltaTxS_type_p = &p_pickinfo->type;
		if (p_pickinfo->offset == offsetof(struct wrsPtpDataTable_s, wrsPtpDeltaRxS))
			wrsPtpDeltaRxS_type_p = &p_pickinfo->type;
	}
}

#define TT_OID WRSPTPDATATABLE_OID
#define TT_PICKINFO wrsPtpDataTable_pickinfo
#define TT_DATA_FILL_FUNC wrsPtpDataTable_data_fill
#define TT_DATA_ARRAY wrsPtpDataTable_array
#define TT_GROUP_NAME "wrsPtpDataTable"
#define TT_INIT_FUNC init_wrsPtpDataTable
#define TT_CACHE_TIMEOUT WRSPTPDATATABLE_CACHE_TIMEOUT
#define TT_POST_INIT_FUNC post_init_wrsPtpDataTable

#include "wrsTableTemplate.h"

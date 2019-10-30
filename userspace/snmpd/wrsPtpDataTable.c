#include "wrsSnmp.h"
#include "snmp_shmem.h"
#include "wrsPtpDataTable.h"

struct wrsPtpDataTable_s wrsPtpDataTable_array[WRS_MAX_N_SERVO_INSTANCES];

static struct pickinfo wrsPtpDataTable_pickinfo[] = {
	/* Warning: strings are a special case for snmp format */
	FIELD(wrsPtpDataTable_s, ASN_UNSIGNED, wrsPtpDataIndex), /* not reported */
	FIELD(wrsPtpDataTable_s, ASN_OCTET_STR, wrsPtpPortName),
	FIELD(wrsPtpDataTable_s, ASN_OCTET_STR, wrsPtpGrandmasterID),
	FIELD(wrsPtpDataTable_s, ASN_OCTET_STR, wrsPtpOwnID),
	FIELD(wrsPtpDataTable_s, ASN_INTEGER, wrsPtpMode),
	FIELD(wrsPtpDataTable_s, ASN_OCTET_STR, wrsPtpServoState),
	FIELD(wrsPtpDataTable_s, ASN_INTEGER, wrsPtpServoStateN),
	FIELD(wrsPtpDataTable_s, ASN_INTEGER, wrsPtpPhaseTracking),
	FIELD(wrsPtpDataTable_s, ASN_OCTET_STR, wrsPtpSyncSource),
	FIELD(wrsPtpDataTable_s, ASN_COUNTER64, wrsPtpClockOffsetPs),
	FIELD(wrsPtpDataTable_s, ASN_INTEGER, wrsPtpClockOffsetPsHR),
	FIELD(wrsPtpDataTable_s, ASN_INTEGER, wrsPtpSkew),
	FIELD(wrsPtpDataTable_s, ASN_COUNTER64, wrsPtpRTT),
	FIELD(wrsPtpDataTable_s, ASN_UNSIGNED, wrsPtpLinkLength),
	FIELD(wrsPtpDataTable_s, ASN_COUNTER, wrsPtpServoUpdates),
	FIELD(wrsPtpDataTable_s, ASN_INTEGER, wrsPtpDeltaTxM),
	FIELD(wrsPtpDataTable_s, ASN_INTEGER, wrsPtpDeltaRxM),
	FIELD(wrsPtpDataTable_s, ASN_INTEGER, wrsPtpDeltaTxS),
	FIELD(wrsPtpDataTable_s, ASN_INTEGER, wrsPtpDeltaRxS),
	FIELD(wrsPtpDataTable_s, ASN_COUNTER, wrsPtpServoStateErrCnt),
	FIELD(wrsPtpDataTable_s, ASN_COUNTER, wrsPtpClockOffsetErrCnt),
	FIELD(wrsPtpDataTable_s, ASN_COUNTER, wrsPtpRTTErrCnt),
	FIELD(wrsPtpDataTable_s, ASN_COUNTER64, wrsPtpServoUpdateTime),
	FIELD(wrsPtpDataTable_s, ASN_INTEGER, wrsPtpServoExt),

};

//FIXME: make a library in ppsi with all such functions, use it all around
int64_t pp_time_to_picos(struct pp_time *ts)
{
	return ts->secs * PP_NSEC_PER_SEC
		+ ((ts->scaled_nsecs * 1000 + 0x8000) >> TIME_INTERVAL_FRACBITS);
}


static int32_t int_saturate(int64_t value)
{
	if (value >= INT32_MAX)
		return INT32_MAX;
	else if (value <= INT32_MIN)
		return INT32_MIN;

	return value;
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
	struct wr_data *wr_d;
	struct wr_servo_ext *wr_servo;
	struct wrh_servo_t *wrh_servo;
	char *tmp_name;

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

				/* wrsPtpMode */
				//TODO

				/* wrsPtpServoState */
				strncpy(ptp_a[si].wrsPtpServoState,
				ppsi_servo->servo_state_name,
				sizeof(ppsi_servo->servo_state_name));

				/* wrsPtpServoStateN */
				ptp_a[si].wrsPtpServoStateN = ppsi_servo->state;

				/* wrsPtpClockOffsetPs */
				ptp_a[si].wrsPtpClockOffsetPs =
				pp_time_to_picos(&ppsi_servo->offsetFromMaster);

				/* wrsPtpClockOffsetPsHR */
				ptp_a[si].wrsPtpClockOffsetPsHR =
				int_saturate(ptp_a[si].wrsPtpClockOffsetPs);

				/* wrsPtpRTT */
				ptp_a[si].wrsPtpRTT = 2*
				pp_time_to_picos(&ppsi_servo->meanDelay);

				/* wrsPtpLinkLength */
				ptp_a[si].wrsPtpLinkLength =
				(uint32_t)(pp_time_to_picos(&ppsi_servo->delayMS)
				/1e12 * 300e6 / 1.55);

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
				if (ppsi_i->protocol_extension == PPSI_EXT_WR)
                                {
					wr_d       = (struct wr_data *)
							wrs_shm_follow(ppsi_head,
							ppsi_i->ext_data);
					wr_servo   = &wr_d->servo_ext;
					wrh_servo  = &wr_d->servo;

					/* wrsPtpPhaseTracking */
					ptp_a[si].wrsPtpPhaseTracking =
					1 + wrh_servo->tracking_enabled;

					/* wrsPtpSyncSource */
					// TODO

					/* wrsPtpSkew */
					ptp_a[si].wrsPtpSkew =
					int_saturate(wrh_servo->skew_ps);

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

					/* wrsPtpServoStateErrCnt */
					ptp_a[si].wrsPtpServoStateErrCnt =
					wrh_servo->n_err_state;

					/* wrsPtpClockOffsetErrCnt */
					ptp_a[si].wrsPtpClockOffsetErrCnt =
                                	wrh_servo->n_err_offset;

					/* wrsPtpRTTErrCnt */
					ptp_a[si].wrsPtpRTTErrCnt =
					wrh_servo->n_err_delta_rtt;
				}
				else
                                {
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

#define TT_OID WRSPTPDATATABLE_OID
#define TT_PICKINFO wrsPtpDataTable_pickinfo
#define TT_DATA_FILL_FUNC wrsPtpDataTable_data_fill
#define TT_DATA_ARRAY wrsPtpDataTable_array
#define TT_GROUP_NAME "wrsPtpDataTable"
#define TT_INIT_FUNC init_wrsPtpDataTable
#define TT_CACHE_TIMEOUT WRSPTPDATATABLE_CACHE_TIMEOUT

#include "wrsTableTemplate.h"

#include "wrsSnmp.h"
#include "snmp_shmem.h"
#include "wrsPortStatusTable.h"

/* Our data: per-port information */
struct wrsPortStatusTable_s wrsPortStatusTable_array[WRS_N_PORTS];

static char *slog_obj_name;
static char *wrsPortStatusSfpError_str = "wrsPortStatusSfpError";

static struct pickinfo wrsPortStatusTable_pickinfo[] = {
	FIELD(wrsPortStatusTable_s, ASN_UNSIGNED, index), /* not reported */
	FIELD(wrsPortStatusTable_s, ASN_OCTET_STR, wrsPortStatusPortName),
	FIELD(wrsPortStatusTable_s, ASN_INTEGER, wrsPortStatusLink),
	FIELD(wrsPortStatusTable_s, ASN_INTEGER, wrsPortStatusConfiguredMode),
	FIELD(wrsPortStatusTable_s, ASN_INTEGER, wrsPortStatusLocked),
	FIELD(wrsPortStatusTable_s, ASN_OCTET_STR, wrsPortStatusPeer),
	FIELD(wrsPortStatusTable_s, ASN_OCTET_STR, wrsPortStatusSfpVN),
	FIELD(wrsPortStatusTable_s, ASN_OCTET_STR, wrsPortStatusSfpPN),
	FIELD(wrsPortStatusTable_s, ASN_OCTET_STR, wrsPortStatusSfpVS),
	FIELD(wrsPortStatusTable_s, ASN_INTEGER, wrsPortStatusSfpInDB),
	FIELD(wrsPortStatusTable_s, ASN_INTEGER, wrsPortStatusSfpGbE),
	FIELD(wrsPortStatusTable_s, ASN_INTEGER, wrsPortStatusSfpError),
	FIELD(wrsPortStatusTable_s, ASN_COUNTER, wrsPortStatusPtpTxFrames),
	FIELD(wrsPortStatusTable_s, ASN_COUNTER, wrsPortStatusPtpRxFrames),
	FIELD(wrsPortStatusTable_s, ASN_INTEGER, wrsPortStatusSfpDom),
	FIELD(wrsPortStatusTable_s, ASN_INTEGER, wrsPortStatusSfpTemp),
	FIELD(wrsPortStatusTable_s, ASN_INTEGER, wrsPortStatusSfpVcc),
	FIELD(wrsPortStatusTable_s, ASN_INTEGER, wrsPortStatusSfpTxBias),
	FIELD(wrsPortStatusTable_s, ASN_INTEGER, wrsPortStatusSfpTxPower),
	FIELD(wrsPortStatusTable_s, ASN_INTEGER, wrsPortStatusSfpRxPower),
};


time_t wrsPortStatusTable_data_fill(unsigned int *n_rows)
{
	unsigned ii, i, ppi_i;
	unsigned retries = 0;
	static time_t time_update;
	time_t time_cur;
	char *ppsi_iface_name;
	static int n_rows_local = 0;

	/* number of rows does not change for wrsPortStatusTable */
	if (n_rows)
		*n_rows = n_rows_local;

	time_cur = get_monotonic_sec();
	if (time_update
	    && time_cur - time_update < WRSPORTSTATUSTABLE_CACHE_TIMEOUT) {
		/* cache not updated, return last update time */
		return time_update;
	}
	time_update = time_cur;

	struct hal_port_state *port_state;
	memset(wrsPortStatusTable_array, 0, sizeof(wrsPortStatusTable_array));

	/* check whether shmem is available */
	if (!shmem_ready_hald()) {
		/* there was an update, return current time */
		snmp_log(LOG_ERR, "%s: Unable to read HAL shmem\n", __func__);
		n_rows_local = 0;
		return time_cur;
	} else {
		n_rows_local = WRS_N_PORTS;
	}

	if (n_rows)
		*n_rows = n_rows_local;

	/* read data, with the sequential lock to have all data consistent */
	while (1) {
		ii = wrs_shm_seqbegin(hal_head);
		for (i = 0; i < hal_nports_local; ++i) {
			/* Assume that number of ports does not change between
			 * reads */
			snprintf(wrsPortStatusTable_array[i].wrsPortStatusPortName, 10,
				 "wri%d", i + 1);
			port_state = hal_lookup_port(hal_ports,
					hal_nports_local,
					wrsPortStatusTable_array[i].wrsPortStatusPortName);
			if(!port_state) {
				/* It looks like we're in strange situation
				 * that HAL is up but hal_ports is not filled
				 */
				continue;
			}
			/* No need to copy all ports structures, only what
			 * we're interested in.
			 * Keep value 0 for Not available
			 * values defined as WRS_PORT_STATUS_LINK_*
			*/
			wrsPortStatusTable_array[i].wrsPortStatusLink =
					1 + state_up(port_state->state);
			if (port_state->state == HAL_PORT_STATE_DISABLED) {
				wrsPortStatusTable_array[i].wrsPortStatusSfpError =
					  WRS_PORT_STATUS_SFP_ERROR_PORT_DOWN;
				/* if port is disabled don't fill
				 * other fields */
				continue;
			}
			/* Keep value 0 for Not available */
			wrsPortStatusTable_array[i].wrsPortStatusLocked =
							1 + port_state->locked;
			/* FIXME: get real peer_id */
			memset(&wrsPortStatusTable_array[i].wrsPortStatusPeer, 0xff,
			       sizeof(ClockIdentity));
			if (port_state->calib.sfp.flags & SFP_FLAG_IN_DB) {
				wrsPortStatusTable_array[i].wrsPortStatusSfpInDB =
					WRS_PORT_STATUS_SFP_IN_DB_IN_DATA_BASE;
			} else {
				wrsPortStatusTable_array[i].wrsPortStatusSfpInDB =
					WRS_PORT_STATUS_SFP_IN_DB_NOT_IN_DATA_BASE;
			}
			if (port_state->calib.sfp.flags & SFP_FLAG_1GbE) {
				wrsPortStatusTable_array[i].wrsPortStatusSfpGbE =
					WRS_PORT_STATUS_SFP_GBE_LINK_GBE;
			} else {
				wrsPortStatusTable_array[i].wrsPortStatusSfpGbE =
					WRS_PORT_STATUS_SFP_GBE_LINK_NOT_GBE;
			}
			strncpy(wrsPortStatusTable_array[i].wrsPortStatusSfpVN,
				port_state->calib.sfp.vendor_name,
				sizeof(wrsPortStatusTable_array[i].wrsPortStatusSfpVN));
			strncpy(wrsPortStatusTable_array[i].wrsPortStatusSfpPN,
				port_state->calib.sfp.part_num,
				sizeof(wrsPortStatusTable_array[i].wrsPortStatusSfpPN));
			strncpy(wrsPortStatusTable_array[i].wrsPortStatusSfpVS,
				port_state->calib.sfp.vendor_serial,
				sizeof(wrsPortStatusTable_array[i].wrsPortStatusSfpVS));

			/* Copy DOM data for SFP */
			if (hal_shmem->read_sfp_diag) {
				if (port_state->has_sfp_diag) {
					wrsPortStatusTable_array[i].wrsPortStatusSfpDom = WRS_PORT_STATUS_SFP_DOM_ENABLE;
					/* temp in C */
					wrsPortStatusTable_array[i].wrsPortStatusSfpTemp = ntohs(*port_state->calib.sfp_dom_raw.temp)/256;
					/* vcc in mV */
					wrsPortStatusTable_array[i].wrsPortStatusSfpVcc = ntohs(*port_state->calib.sfp_dom_raw.vcc)/10;
					/* tx_bias in uA */
					wrsPortStatusTable_array[i].wrsPortStatusSfpTxBias = ntohs(*port_state->calib.sfp_dom_raw.tx_bias)*2;
					/* tx_pow in uW */
					wrsPortStatusTable_array[i].wrsPortStatusSfpTxPower = ntohs(*port_state->calib.sfp_dom_raw.tx_pow)/10;
					/* rx_pow in uW */
					wrsPortStatusTable_array[i].wrsPortStatusSfpRxPower = ntohs(*port_state->calib.sfp_dom_raw.rx_pow)/10;
				} else {
					wrsPortStatusTable_array[i].wrsPortStatusSfpDom = WRS_PORT_STATUS_SFP_DOM_NOT_SUPPORTED;
				}
			} else {
				wrsPortStatusTable_array[i].wrsPortStatusSfpDom = WRS_PORT_STATUS_SFP_DOM_DISABLE;
			}
		}

		retries++;
		if (retries > 100) {
			snmp_log(LOG_ERR, "%s: Unable to read HAL, too many retries\n",
				 __func__);
			retries = 0;
			}
		if (!wrs_shm_seqretry(hal_head, ii))
			break; /* consistent read */
		usleep(1000);
	}

	/* keep the log printouts outside wrs_shm_seqbegin and wrs_shm_seqretry
	 * since it introduces delays and might lead to an infinite loop of
	 * retries */
	slog_obj_name = wrsPortStatusSfpError_str;
	for (i = 0; i < hal_nports_local; ++i) {
		/* If info about wrsPortStatusSfpGbE is not filled skip further
		 * checking. NOTE: there is no need to check the fill of others
		 * like:
		 * - wrsPortStatusConfiguredMode
		 * - wrsPortStatusSfpInDB
		 */
		/* Don't check if WRS_PORT_STATUS_SFP_ERROR_PORT_DOWN */
		if (wrsPortStatusTable_array[i].wrsPortStatusSfpGbE == 0
		    || wrsPortStatusTable_array[i].wrsPortStatusSfpError == WRS_PORT_STATUS_SFP_ERROR_PORT_DOWN) {
			continue;
		}

		/* sfp error when SFP is not 1 GbE or
		  * (port is not "non-wr", "none" mode and sfp not in data base)
		  * port down, is set above
		  * (WRS_PORT_STATUS_SFP_ERROR_PORT_DOWN) */
		wrsPortStatusTable_array[i].wrsPortStatusSfpError = WRS_PORT_STATUS_SFP_ERROR_SFP_OK;
		if (wrsPortStatusTable_array[i].wrsPortStatusSfpGbE == WRS_PORT_STATUS_SFP_GBE_LINK_NOT_GBE) {
			/* error, SFP is not 1 GbE */
			wrsPortStatusTable_array[i].wrsPortStatusSfpError = WRS_PORT_STATUS_SFP_ERROR_SFP_ERROR;
			snmp_log(LOG_ERR, "SNMP: " SL_ER  " %s: "
				  "SFP in port %d (wri%d) is not for Gigabit Ethernet\n",
				  slog_obj_name, i + 1, i + 1);
		}
		if ((wrsPortStatusTable_array[i].wrsPortStatusConfiguredMode != WRS_PORT_STATUS_CONFIGURED_MODE_NON_WR)
		    && (wrsPortStatusTable_array[i].wrsPortStatusConfiguredMode != WRS_PORT_STATUS_CONFIGURED_MODE_NONE)
		    && (wrsPortStatusTable_array[i].wrsPortStatusSfpInDB == WRS_PORT_STATUS_SFP_IN_DB_NOT_IN_DATA_BASE)) {
			/* error, port is not non-wr mode and sfp not in data base */
			wrsPortStatusTable_array[i].wrsPortStatusSfpError = WRS_PORT_STATUS_SFP_ERROR_SFP_ERROR;
			snmp_log(LOG_ERR, "SNMP: " SL_ER  " %s: "
				  "SFP in port %d (wri%d) is not in the database. "
				  "Change the SFP or declare port as non-wr or none\n",
				  slog_obj_name, i + 1, i + 1);
		}

		snmp_log(LOG_DEBUG, "reading ports name %s link %d, "
			"mode %d, locked %d\n",
			wrsPortStatusTable_array[i].wrsPortStatusPortName,
			wrsPortStatusTable_array[i].wrsPortStatusLink,
			wrsPortStatusTable_array[i].wrsPortStatusConfiguredMode,
			wrsPortStatusTable_array[i].wrsPortStatusLocked);
	}

	retries = 0;
	/* check whether shmem is available */
	if (!shmem_ready_ppsi()) {
		/* there was an update, return current time */
		snmp_log(LOG_ERR, "%s: Unable to read PPSI shmem\n",
			 __func__);
		return time_cur;
	}

	/* fill wrsPortStatusPtpTxFrames and wrsPortStatusPtpRxFrames
	 * ptp_tx_count and ptp_rx_count statistics in PPSI are collected per
	 * ppi instance. Since there can be more than one instance per physical
	 * port, proper counters has to be added. */
	while (1) {
		ii = wrs_shm_seqbegin(ppsi_head);
		/* Match port name with interface name of ppsi instance.
		 * More than one ppsi_iface_name can match to
		 * wrsPortStatusTable_array[i].wrsPortStatusPortName, but only one can
		 * match way round */
		for (ppi_i = 0; ppi_i < *ppsi_ppi_nlinks; ppi_i++) {
			/* (ppsi_ppi + ppi_i)->iface_name is a pointer in
			 * shmem, so we have to follow it
			 * NOTE: ppi->cfg.port_name cannot be used instead,
			 * because it is not used when ppsi is configured from
			 * cmdline */
			ppsi_iface_name = (char *) wrs_shm_follow(ppsi_head,
					       (ppsi_ppi + ppi_i)->iface_name);
			for (i = 0; i < hal_nports_local; ++i) {
				if (!strncmp(wrsPortStatusTable_array[i].wrsPortStatusPortName,
					     ppsi_iface_name, 12)) {
					wrsPortStatusTable_array[i].wrsPortStatusPtpTxFrames +=
					      (ppsi_ppi + ppi_i)->ptp_tx_count;
					wrsPortStatusTable_array[i].wrsPortStatusPtpRxFrames +=
					      (ppsi_ppi + ppi_i)->ptp_rx_count;
					/* speed up a little, break here */
					break;
				}
			}
		}
		retries++;
		if (retries > 100) {
			snmp_log(LOG_ERR, "%s: Unable to read PPSI, too many retries\n",
					   __func__);
			retries = 0;
			break;
			}
		if (!wrs_shm_seqretry(ppsi_head, ii))
			break; /* consistent read */
		usleep(1000);
	}

	/* there was an update, return current time */
	return time_cur;
}

#define TT_OID WRSPORTSTATUSTABLE_OID
#define TT_PICKINFO wrsPortStatusTable_pickinfo
#define TT_DATA_FILL_FUNC wrsPortStatusTable_data_fill
#define TT_DATA_ARRAY wrsPortStatusTable_array
#define TT_GROUP_NAME "wrsPortStatusTable"
#define TT_INIT_FUNC init_wrsPortStatusTable
#define TT_CACHE_TIMEOUT WRSPORTSTATUSTABLE_CACHE_TIMEOUT


#include "wrsTableTemplate.h"

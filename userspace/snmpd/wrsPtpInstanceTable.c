#include "wrsSnmp.h"
#include "snmp_shmem.h"
#include "wrsPortStatusTable.h"
#include "wrsPtpInstanceTable.h"

struct wrsPtpInstanceTable_s wrsPtpInstanceTable_array[PP_MAX_LINKS];
// global:
// defaultDS_t:
// --externalPortConfigurationEnabled
// --slaveOnly


static struct pickinfo wrsPtpInstanceTable_pickinfo[] = {
	/* Warning: strings are a special case for snmp format */
	FIELD(wrsPtpInstanceTable_s, ASN_UNSIGNED,  wrsPtpInstancePortIndex), /* not reported */
	FIELD(wrsPtpInstanceTable_s, ASN_UNSIGNED,  wrsPtpInstanceOnPortIndex), /* not reported */
	FIELD(wrsPtpInstanceTable_s, ASN_OCTET_STR, wrsPtpInstanceName),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER,   wrsPtpInstancePort),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER,   wrsPtpInstancePortInstance),
	FIELD(wrsPtpInstanceTable_s, ASN_OCTET_STR, wrsPtpInstancePortName),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER,   wrsPtpInstanceState),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER,   wrsPtpInstanceMasterOnly),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER,   wrsPtpInstanceExtPortCfgDesSt),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER,   wrsPtpInstanceMechanism),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER,   wrsPtpInstanceProfile),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER,   wrsPtpInstanceExtension),

	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER,   wrsPtpInstanceAsymEnabled),
	FIELD(wrsPtpInstanceTable_s, ASN_COUNTER64, wrsPtpInstanceAsymConstAsym),
	FIELD(wrsPtpInstanceTable_s, ASN_COUNTER64, wrsPtpInstanceAsymConstAsymPS),
	FIELD(wrsPtpInstanceTable_s, ASN_COUNTER64, wrsPtpInstanceAsymScDelayCoef),
	FIELD(wrsPtpInstanceTable_s, ASN_OCTET_STR, wrsPtpInstanceAsymScDelayCoefHR),
	FIELD(wrsPtpInstanceTable_s, ASN_COUNTER64, wrsPtpInstanceTSCorrEgressLat),
	FIELD(wrsPtpInstanceTable_s, ASN_COUNTER64, wrsPtpInstanceTSCorrEgressLatPS),
	FIELD(wrsPtpInstanceTable_s, ASN_COUNTER64, wrsPtpInstanceTSCorrIngLat),
	FIELD(wrsPtpInstanceTable_s, ASN_COUNTER64, wrsPtpInstanceTSCorrIngLatPS),
	FIELD(wrsPtpInstanceTable_s, ASN_COUNTER64, wrsPtpInstanceTSCorrSemistLat),
	FIELD(wrsPtpInstanceTable_s, ASN_COUNTER64, wrsPtpInstanceTSCorrSemistLatPS),

	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER,   wrsPtpInstanceExtState),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER,   wrsPtpInstanceProtoDetectState),
	FIELD(wrsPtpInstanceTable_s, ASN_OCTET_STR, wrsPtpInstancePeerMac),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER,   wrsPtpInstancePeerVid),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER,   wrsPtpInstanceVlanNum),
	FIELD(wrsPtpInstanceTable_s, ASN_OCTET_STR, wrsPtpInstanceVlanListStr),

	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER,   wrsPtpInstanceStatusError),
};
static char *slog_obj_name;
static char *wrsPtpInstanceStatusError_str = "wrsPtpInstanceStatusError";

static inline struct hal_port_state *pp_wrs_lookup_port(char *name)
{
	int i;

	for (i = 0; i < hal_nports_local; i++)
		if (hal_ports[i].in_use &&!strcmp(name, hal_ports[i].name))
                        return hal_ports + i;
	return NULL;
}

time_t wrsPtpInstanceTable_data_fill(unsigned int *n_rows)
{
	unsigned ii, i;
	unsigned retries = 0;
	static time_t time_update;
	time_t time_cur;
	static int n_rows_local = 0;
	struct wrsPtpInstanceTable_s *i_a;
	struct pp_instance *ppsi_i;
	struct wrsPortStatusTable_s *p_a;
	char *tmp_name;
	portDS_t *portDS_i;
	struct hal_port_state *p;
	int phys_port=0;
	int last_port = 0;
	int instance_on_port = 0;
	char *tmpstr_p;
	int vlan_i;
        float tmp_f;
	slog_obj_name = wrsPtpInstanceStatusError_str;

	/* number of rows does not change for wrsPortStatusTable */
	if (n_rows)
		*n_rows = n_rows_local;

	time_cur = get_monotonic_sec();
	if (time_update
	    && time_cur - time_update < WRSPTPINSTANCETABLE_CACHE_TIMEOUT) {
		/* cache not updated, return last update time */
		return time_update;
	}
	time_update = time_cur;

	memset(&wrsPtpInstanceTable_array, 0, sizeof(wrsPtpInstanceTable_array));

	i_a = wrsPtpInstanceTable_array;
	p_a = wrsPortStatusTable_array;

	/* check whether shmem is available */
	if (!shmem_ready_ppsi() || ppsi_ppi_nlinks==NULL) {
		snmp_log(LOG_ERR, "%s: Unable to read PPSI's shmem\n", __func__);
		/* If set to 0 all PPSI related OIDs disappear */
		n_rows_local = 0;
		return time_update;
	} else {
		n_rows_local = *ppsi_ppi_nlinks;
	}

	if (n_rows)
		*n_rows = n_rows_local;

	while (1) {
		ii = wrs_shm_seqbegin(ppsi_head);
		for (i = 0; i < *ppsi_ppi_nlinks; i++) {

			ppsi_i = ppsi_ppi + i;
			/* (ppsi_ppi + i)->iface_name is a pointer in
			 * shmem, so we have to follow it
			 * NOTE: ppsi_i->cfg.port_name cannot be used instead,
			 * because it is not used when ppsi is configured from
			 * cmdline */

			tmp_name = (char *) wrs_shm_follow(ppsi_head,
					       ppsi_i->port_name);
			strncpy(i_a[i].wrsPtpInstanceName, tmp_name, 16);
			i_a[i].wrsPtpInstanceName[15] = '\0';

			tmp_name = (char *) wrs_shm_follow(ppsi_head,
					       ppsi_i->iface_name);
			strncpy(i_a[i].wrsPtpInstancePortName, tmp_name, 16);
			i_a[i].wrsPtpInstancePortName[15] = '\0';

			p = pp_wrs_lookup_port(tmp_name);
			if (p) {
				phys_port = p->hw_index + 1;
				i_a[i].wrsPtpInstancePort = phys_port;
				
				if (last_port == phys_port)
					instance_on_port++;
				else
					instance_on_port = 1;
				
				last_port = phys_port;
			}

			i_a[i].wrsPtpInstancePortInstance = instance_on_port;

			i_a[i].wrsPtpInstanceState = ppsi_i->state;
			/* follow portDS */
			portDS_i = (portDS_t *) wrs_shm_follow(ppsi_head,
							       ppsi_i->portDS);
			if (portDS_i)
				i_a[i].wrsPtpInstanceMasterOnly = portDS_i->masterOnly ? 2 : 1 ;
			else 
				i_a[i].wrsPtpInstanceMasterOnly = 0;

			i_a[i].wrsPtpInstanceExtPortCfgDesSt = ppsi_i->externalPortConfigurationPortDS.desiredState;
			i_a[i].wrsPtpInstanceMechanism = ppsi_i->delayMechanism;
			i_a[i].wrsPtpInstanceProfile = ppsi_i->cfg.profile + 1;
			i_a[i].wrsPtpInstanceExtension = ppsi_i->protocol_extension+1;
			i_a[i].wrsPtpInstanceAsymEnabled = ppsi_i->asymmetryCorrectionPortDS.enable + 1;
			i_a[i].wrsPtpInstanceAsymConstAsym = ppsi_i->asymmetryCorrectionPortDS.constantAsymmetry;
			i_a[i].wrsPtpInstanceAsymConstAsymPS = (int64_t)((((float)ppsi_i->asymmetryCorrectionPortDS.constantAsymmetry)/(1<<16))*1000);
			i_a[i].wrsPtpInstanceAsymScDelayCoef = ppsi_i->asymmetryCorrectionPortDS.scaledDelayCoefficient;
			tmpstr_p = i_a[i].wrsPtpInstanceAsymScDelayCoefHR;
			tmp_f    = ((float)ppsi_i->asymmetryCorrectionPortDS.scaledDelayCoefficient)/(((uint64_t)1<<62));
			snprintf(tmpstr_p, 64, "%f,", tmp_f);
			
			i_a[i].wrsPtpInstanceTSCorrEgressLat = ppsi_i->timestampCorrectionPortDS.egressLatency;
			i_a[i].wrsPtpInstanceTSCorrEgressLatPS =(int64_t)((((float)ppsi_i->timestampCorrectionPortDS.egressLatency)/(1<<16))*1000);
			i_a[i].wrsPtpInstanceTSCorrIngLat = ppsi_i->timestampCorrectionPortDS.ingressLatency;
			i_a[i].wrsPtpInstanceTSCorrIngLatPS = (int64_t)((((float)ppsi_i->timestampCorrectionPortDS.ingressLatency)/(1<<16))*1000);
			i_a[i].wrsPtpInstanceTSCorrSemistLat = ppsi_i->timestampCorrectionPortDS.semistaticLatency;
			i_a[i].wrsPtpInstanceTSCorrSemistLatPS =(int64_t)((((float)ppsi_i->timestampCorrectionPortDS.semistaticLatency)/(1<<16))*1000);

			i_a[i].wrsPtpInstanceProtoDetectState = ppsi_i->pdstate+1;
			i_a[i].wrsPtpInstanceExtState = ppsi_i->extState+1;

			memcpy(i_a[i].wrsPtpInstancePeerMac, ppsi_i->peer, ETH_ALEN);
			i_a[i].wrsPtpInstancePeerVid = ppsi_i->peer_vid;

			i_a[i].wrsPtpInstanceVlanNum = ppsi_i->nvlans;
			tmpstr_p = i_a[i].wrsPtpInstanceVlanListStr;
			for (vlan_i = 0; vlan_i < ppsi_i->nvlans; vlan_i++){
				int ret_len;
				int str_space_left;

				str_space_left = WRSPTPINSTANCEVLANLISTSTRLEN - (tmpstr_p - i_a[i].wrsPtpInstanceVlanListStr);
				ret_len = snprintf(tmpstr_p, str_space_left, "%d,", ppsi_i->vlans[vlan_i]);
				tmpstr_p += ret_len;
			}
			if (ppsi_i->nvlans) {
				/* remove trailing comma */
				int list_len;
				char *last_char;

				list_len = strnlen(i_a[i].wrsPtpInstanceVlanListStr,
						   WRSPTPINSTANCEVLANLISTSTRLEN);
				
				last_char = &i_a[i].wrsPtpInstanceVlanListStr[list_len - 1];
				if (*last_char == ',')
					*last_char = 0;
			}
			
			if (shmem_ready_hald())
                        {
				i_a[i].wrsPtpInstanceStatusError = WRS_SLAVE_LINK_STATUS_OK;

				/* error when there is an active Slave port in GM of FM mode */
				if ((p_a[phys_port-1].wrsPortStatusMonitor != WRS_PORT_STATUS_MONITOR_DISABLE) &&
				    (p_a[phys_port-1].wrsPortStatusLink == WRS_PORT_STATUS_LINK_UP))
				{
					if ((i_a[i].wrsPtpInstanceState == PPS_SLAVE ||
					     i_a[i].wrsPtpInstanceState == PPS_UNCALIBRATED) &&
					    (hal_shmem->hal_mode        == HAL_TIMING_MODE_GRAND_MASTER))
					{
						i_a[i].wrsPtpInstanceStatusError = WRS_SLAVE_LINK_STATUS_ERROR;
						snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
							 "In Grand Master mode, instance %d on port %d (%s) "
							 "is in SLAVE or UNCALIBRATED state.\n",
							 slog_obj_name, i, phys_port, i_a[i].wrsPtpInstancePortName);
					}
					if ((i_a[i].wrsPtpInstanceState == PPS_SLAVE ||
					     i_a[i].wrsPtpInstanceState == PPS_UNCALIBRATED) &&
					    (hal_shmem->hal_mode        == HAL_TIMING_MODE_FREE_MASTER)) // THIS is wrogn
					{
						i_a[i].wrsPtpInstanceStatusError = WRS_SLAVE_LINK_STATUS_ERROR;
						snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
							 "In Free Running mode, instance %d on port %d (%s) "
							 "is in SLAVE or UNCALIBRATED state.\n",
							 slog_obj_name, i, phys_port, i_a[i].wrsPtpInstancePortName);
					}
				}

				if ((p_a[phys_port-1].wrsPortStatusMonitor != WRS_PORT_STATUS_MONITOR_DISABLE) &&
				    (hal_shmem->hal_mode == HAL_TIMING_MODE_BC) &&
				    (i_a[i].wrsPtpInstanceExtPortCfgDesSt == PPS_SLAVE))
				{
					if(i_a[i].wrsPtpInstanceState != PPS_DISABLED &&
					   i_a[i].wrsPtpInstanceState != PPS_SLAVE)
					{
						i_a[i].wrsPtpInstanceStatusError = WRS_SLAVE_LINK_STATUS_ERROR;
						snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
							 "In Boundary Clock mode, External Port Configuration is enabled "
							 "and desiredState is set to SLAVE but instance %d on port %d (%s) "
							 "is not in SLAVE state.\n",
							 slog_obj_name, i, phys_port, i_a[i].wrsPtpInstancePortName);
					}
					if(p_a[phys_port-1].wrsPortStatusLink == WRS_PORT_STATUS_LINK_DOWN)
					{
						i_a[i].wrsPtpInstanceStatusError = WRS_SLAVE_LINK_STATUS_ERROR;
						snmp_log(LOG_ERR, "SNMP: " SL_ER " %s: "
							 "In Boundary Clock mode, External Port Configuration is enabled "
							 "and desiredState is set to SLAVE on instance %d, yet port %d (%s) "
							 "is DOWN.\n",
							 slog_obj_name, i, phys_port, i_a[i].wrsPtpInstancePortName);
					}
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

/* global variable to keep number of rows, filled by cache function
 * one for each table */
static unsigned int t_n_rows; /* template n_rows */


static netsnmp_variable_list *
table_next_entry(void **loop_context,
		void **data_context,
		netsnmp_variable_list *index,
		netsnmp_iterator_info *data)
{
	int i = (int)(intptr_t)(*loop_context);

	netsnmp_variable_list *idx = index;
// 	snmp_log(LOG_ERR, "%s: loop_context %d\n", __func__, i);
	if (i >= t_n_rows) {
		return NULL; /* no more */
		snmp_log(LOG_ERR, "%s: no more\n", __func__);
	}

	/* Embed into index part of OID a port number */
	snmp_set_var_typed_value(idx, ASN_INTEGER,
            (u_char *)&wrsPtpInstanceTable_array[i].wrsPtpInstancePort,
            sizeof(uint32_t));

	/* Embed into index part of OID an instance index on port */
	idx = idx->next_variable;
	snmp_set_var_typed_value(idx, ASN_INTEGER,
            (u_char *)&wrsPtpInstanceTable_array[i].wrsPtpInstancePortInstance,
            sizeof(uint32_t));
	
	*data_context = &wrsPtpInstanceTable_array[i];
	*loop_context = (void *)(intptr_t)(++i);

	return index;
}

static netsnmp_variable_list *
table_first_entry(void **loop_context,
		  void **data_context,
		  netsnmp_variable_list *index,
		  netsnmp_iterator_info *data)
{
	*loop_context = 0;
	*data_context = 0;
// 	snmp_log(LOG_ERR, "%s\n", __func__);
	/* reset internal position, so "next" is "first" */
	return table_next_entry(loop_context, data_context, index, data);
}

static int
table_handler(netsnmp_mib_handler          *handler,
	      netsnmp_handler_registration *reginfo,
	      netsnmp_agent_request_info   *reqinfo,
	      netsnmp_request_info         *requests)
{
	netsnmp_request_info  *request;
	netsnmp_variable_list *requestvb;
	netsnmp_table_request_info *table_info;
	struct wrsPtpInstanceTable_s *entry;

	struct pickinfo *pi;
	int subid;
	int len;
	void *ptr;
	struct counter64 tmp_counter64;

// 	snmp_log(LOG_ERR, "%s: before mode check\n", __func__);
	switch (reqinfo->mode) {
	case MODE_GET:
		/* "break;" so read code is not indented too much */
		break;

	case MODE_GETNEXT:
	case MODE_GETBULK:
	case MODE_SET_RESERVE1:
	case MODE_SET_RESERVE2:
	case MODE_SET_ACTION:
	case MODE_SET_COMMIT:
	case MODE_SET_FREE:
	case MODE_SET_UNDO:
		/* unsupported mode */
		return SNMP_ERR_NOERROR;
	default:
		/* unknown mode */
		return SNMP_ERR_NOERROR;
	}


	for (request = requests; request; request = request->next) {
		requestvb = request->requestvb;

		/* "context" is the row number */
		entry = (struct wrsPtpInstanceTable_s *)netsnmp_extract_iterator_context(request);
                if (!entry) {
			/* NULL returned from
			 * netsnmp_extract_iterator_context shuld be
			 * interpreted as end of table */
			continue;
		}
		
		table_info = netsnmp_extract_table_info(request);
		subid = table_info->colnum - 1;
// 		snmp_log(LOG_ERR, "%s: %d\n", __func__, subid);

		pi = wrsPtpInstanceTable_pickinfo + subid;
		ptr = (void *)(entry) + pi->offset;
		/* snmp_set_var_typed_value function does not support counter64
		 * as a uint64_t, but as a struct counter64. Their binary
		 * representation differs by order of 32bit words. We fill
		 * struct counter64 according to its fields. */
		if (pi->type == ASN_COUNTER64) {
			tmp_counter64.high = (*(uint64_t *)ptr) >> 32;
			tmp_counter64.low = *(uint64_t *)ptr;
			ptr = &tmp_counter64;
		}
		len = pi->len;
		if (len > 8) /* special case for strings */
			len = strnlen(ptr, len);

		snmp_set_var_typed_value(requestvb, pi->type, ptr, len);
	}
	return SNMP_ERR_NOERROR;
}

static int table_cache_load(netsnmp_cache *cache, void *vmagic)
{
	wrsPtpInstanceTable_data_fill(&t_n_rows);
	return 0;
}

void init_wrsPtpInstanceTable(void)
{
	const oid wrsTT_oid[] = { WRSPTPINSTANCETABLE_OID };
	netsnmp_table_registration_info *table_info;
	netsnmp_iterator_info *iinfo;
	netsnmp_handler_registration *reginfo;
	/* do the registration for the table/per-port */
	table_info = SNMP_MALLOC_TYPEDEF(netsnmp_table_registration_info);
	if (!table_info)
		return;

	/* Add indexes: we only use one integer OID member as line identifier */
	netsnmp_table_helper_add_indexes(table_info, ASN_INTEGER, ASN_INTEGER, 0);

	/* first column is index, but don't return it, it is only for MIB */
	table_info->min_column = 3;
	table_info->max_column = ARRAY_SIZE(wrsPtpInstanceTable_pickinfo);

	/* Iterator info */
	iinfo  = SNMP_MALLOC_TYPEDEF(netsnmp_iterator_info);
	if (!iinfo)
		return; /* free table_info? */

	iinfo->get_first_data_point = table_first_entry;
	iinfo->get_next_data_point  = table_next_entry;
	iinfo->table_reginfo        = table_info;

	/* register the table */
	reginfo = netsnmp_create_handler_registration("wrsPtpInstanceTable",
						      table_handler,
						      wrsTT_oid,
						      OID_LENGTH(wrsTT_oid),
						      HANDLER_CAN_RONLY);
	netsnmp_register_table_iterator(reginfo, iinfo);
 
	netsnmp_inject_handler(reginfo,
			netsnmp_get_cache_handler(WRSPTPINSTANCETABLE_CACHE_TIMEOUT,
						  table_cache_load, NULL,
						  wrsTT_oid,
						  OID_LENGTH(wrsTT_oid)));

}


#include "wrsSnmp.h"
#include "snmp_shmem.h"
#include "wrsPtpInstanceTable.h"

struct wrsPtpInstanceTable_s wrsPtpInstanceTable_array[PP_MAX_LINKS];

static struct pickinfo wrsPtpInstanceTable_pickinfo[] = {
	/* Warning: strings are a special case for snmp format */
	FIELD(wrsPtpInstanceTable_s, ASN_UNSIGNED, wrsPtpInstanceIndex), /* not reported */
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER, wrsPtpInstancePort),
	FIELD(wrsPtpInstanceTable_s, ASN_OCTET_STR, wrsPtpInstancePortName),
	FIELD(wrsPtpInstanceTable_s, ASN_OCTET_STR, wrsPtpInstanceName),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER, wrsPtpInstanceState),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER, wrsPtpInstanceStateNext),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER, wrsPtpInstanceRole),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER, wrsPtpInstanceMechanism),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER, wrsPtpInstanceProto),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER, wrsPtpInstanceExt),
	FIELD(wrsPtpInstanceTable_s, ASN_OCTET_STR, wrsPtpInstancePeerMac),
	FIELD(wrsPtpInstanceTable_s, ASN_INTEGER, wrsPtpInstancePeerVid),
};

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
	char *tmp_name;
	struct hal_port_state *p;

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

	/* check whether shmem is available */
	if (!shmem_ready_ppsi() && !ppsi_ppi_nlinks) {
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
			strncpy(i_a[i].wrsPtpInstanceName, tmp_name, 12);
			i_a[i].wrsPtpInstanceName[11] = '\0';

			tmp_name = (char *) wrs_shm_follow(ppsi_head,
					       ppsi_i->iface_name);
			strncpy(i_a[i].wrsPtpInstancePortName, tmp_name, 12);
			i_a[i].wrsPtpInstancePortName[11] = '\0';

			p = pp_wrs_lookup_port(tmp_name);
			if (p)
				i_a[i].wrsPtpInstancePort = p->hw_index + 1;

			i_a[i].wrsPtpInstanceState = ppsi_i->state;
			i_a[i].wrsPtpInstanceStateNext = ppsi_i->next_state;
			i_a[i].wrsPtpInstanceRole = ppsi_i->role + 1;
			i_a[i].wrsPtpInstanceMechanism = ppsi_i->mech + 1;
			i_a[i].wrsPtpInstanceProto = ppsi_i->proto + 1;
			i_a[i].wrsPtpInstanceExt = ppsi_i->cfg.ext + 1;

			memcpy(i_a[i].wrsPtpInstancePeerMac, ppsi_i->peer, ETH_ALEN);
			i_a[i].wrsPtpInstancePeerVid = ppsi_i->peer_vid;
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

struct coordinates_s {
    int port;
    int inst;
};


struct coordinates_s inst_c;

static netsnmp_variable_list *
table_next_entry(void **loop_context,
			void **data_context,
			netsnmp_variable_list *index,
			netsnmp_iterator_info *data)
{
	
	inst_c.inst++;
	if (inst_c.inst > 1){
		inst_c.inst = 0;
		inst_c.port++;
	}
	if (inst_c.port > 5){
		return NULL;
	}
	/* create the line ID from counter number */
// 	i = (intptr_t)*loop_context;
// 	if (i >= t_n_rows)
// 		return NULL; /* no more */
// 	i++;
	/* Create the row OID: only the counter index */
	snmp_set_var_value(index, (u_char *)&inst_c.port, sizeof(inst_c.port));
	snmp_set_var_value(index->next_variable, (u_char *)&inst_c.inst, sizeof(inst_c.inst));

	/* Set the data context (1..4)
	 * Cannot be set to 0, because netsnmp_extract_iterator_context returns
	 * NULL in function wrsPstats_handler when table is over */
	*data_context = (void *)&inst_c;
	/* and set the loop context for the next iteration */
	*loop_context = (void *)&inst_c;
	return index;
}

static netsnmp_variable_list *
table_first_entry(void **loop_context,
			void **data_context,
			netsnmp_variable_list *index,
			netsnmp_iterator_info *data)
{
	inst_c.port = 0;
	inst_c.inst = 0;
	/* reset internal position, so "next" is "first" */
	*loop_context = &inst_c; /* first counter */
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

	struct pickinfo *pi;
	int row, subid;
	int len;
	void *ptr;
	struct counter64 tmp_counter64;

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
		struct coordinates_s *xxx;
		requestvb = request->requestvb;

		/* "context" is the row number */
		xxx = (struct coordinates_s*)netsnmp_extract_iterator_context(request);
		if (!xxx)
			/* NULL returned from
				 * netsnmp_extract_iterator_context shuld be
				 * interpreted as end of table */
			break;
		/* change range of row (1..X (snmp is 1 based) ->
			 * 0..X (wrs_t_table_array/data array is 0 based)) */
		row = 5;
		table_info = netsnmp_extract_table_info(request);
		subid = table_info->colnum - 1;

		pi = wrsPtpInstanceTable_pickinfo + subid;
		ptr = (void *)(wrsPtpInstanceTable_array + row) + pi->offset;
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


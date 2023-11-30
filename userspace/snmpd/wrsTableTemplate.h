/* global variable to keep number of rows, filled by cache function
 * one for each table */
static unsigned int t_n_rows; /* template n_rows */

static netsnmp_variable_list *
table_next_entry(void **loop_context,
			void **data_context,
			netsnmp_variable_list *index,
			netsnmp_iterator_info *data)
{
	intptr_t i;
	/* create the line ID from counter number */
	i = (intptr_t)*loop_context;
	if (i >= t_n_rows)
		return NULL; /* no more */
	i++;
	/* Create the row OID: only the counter index */
	snmp_set_var_value(index, (u_char *)&i, sizeof(i));

	/* Set the data context (1..4)
	 * Cannot be set to 0, because netsnmp_extract_iterator_context returns
	 * NULL in function wrsPstats_handler when table is over */
	*data_context = (void *)i;
	/* and set the loop context for the next iteration */
	*loop_context = (void *)i;
	return index;
}

static netsnmp_variable_list *
table_first_entry(void **loop_context,
			void **data_context,
			netsnmp_variable_list *index,
			netsnmp_iterator_info *data)
{
	/* reset internal position, so "next" is "first" */
	*loop_context = (void *)0; /* first counter */
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
		requestvb = request->requestvb;

		/* "context" is the row number */
		row = (intptr_t)netsnmp_extract_iterator_context(request);
		if (!row)
			/* NULL returned from
				 * netsnmp_extract_iterator_context shuld be
				 * interpreted as end of table */
			break;
		/* change range of row (1..X (snmp is 1 based) ->
			 * 0..X (wrs_t_table_array/data array is 0 based)) */
		row--;
		table_info = netsnmp_extract_table_info(request);
		subid = table_info->colnum - 1;

		pi = TT_PICKINFO + subid;
		ptr = (void *)(TT_DATA_ARRAY + row) + pi->offset;
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
	TT_DATA_FILL_FUNC(&t_n_rows);
	return 0;
}

void TT_INIT_FUNC(void)
{
	const oid wrsTT_oid[] = { TT_OID };
	netsnmp_table_registration_info *table_info;
	netsnmp_iterator_info *iinfo;
	netsnmp_handler_registration *reginfo;
	/* do the registration for the table/per-port */
	table_info = SNMP_MALLOC_TYPEDEF(netsnmp_table_registration_info);
	if (!table_info)
		return;

	/* Add indexes: we only use one integer OID member as line identifier */
	netsnmp_table_helper_add_indexes(table_info, ASN_INTEGER, 0);

	/* first column is index, but don't return it, it is only for MIB */
	table_info->min_column = 2;
	table_info->max_column = ARRAY_SIZE(TT_PICKINFO);

	/* Iterator info */
	iinfo  = SNMP_MALLOC_TYPEDEF(netsnmp_iterator_info);
	if (!iinfo)
		return; /* free table_info? */

	iinfo->get_first_data_point = table_first_entry;
	iinfo->get_next_data_point  = table_next_entry;
	iinfo->table_reginfo        = table_info;

	/* register the table */
	reginfo = netsnmp_create_handler_registration(TT_GROUP_NAME,
						      table_handler,
						      wrsTT_oid,
						      OID_LENGTH(wrsTT_oid),
						      HANDLER_CAN_RONLY);
	netsnmp_register_table_iterator(reginfo, iinfo);

	netsnmp_inject_handler(reginfo,
			netsnmp_get_cache_handler(TT_CACHE_TIMEOUT,
						  table_cache_load, NULL,
						  wrsTT_oid,
						  OID_LENGTH(wrsTT_oid)));

	/* If the function is defined, run it */
#ifdef TT_POST_INIT_FUNC
	TT_POST_INIT_FUNC();
#endif
}

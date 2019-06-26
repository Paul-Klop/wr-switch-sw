static int group_handler(netsnmp_mib_handler          *handler,
			netsnmp_handler_registration *reginfo,
			netsnmp_agent_request_info   *reqinfo,
			netsnmp_request_info         *requests)
{

	int obj; /* the final index */
	struct pickinfo *pi;
	void *ptr;
	int len;
	struct counter64 tmp_counter64;

	/*
	 * Retrieve information from ppsi itself. But this function
	 * is called once for every item, so only query the whole set
	 * once every 2 seconds.
	 */


	GT_DATA_FILL_FUNC();
	switch (reqinfo->mode) {
	case MODE_GET:
		/* "- 2" because last is 0 for all scalars, I suppose */
		obj = requests->requestvb->name[
			requests->requestvb->name_length - 2];
		obj--; /* we are 0-based */
		if (obj < 0 || obj >= ARRAY_SIZE(GT_PICKINFO)) {
			snmp_log(LOG_ERR, "SNMP: " SL_ER
				 "wrong index (%d) in "GT_GROUP_NAME"\n",
				 obj + 1);
			return SNMP_ERR_GENERR;
		}
		pi = GT_PICKINFO + obj;
		ptr = (void *)&GT_DATA_STRUCT + pi->offset;
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
		snmp_set_var_typed_value(requests->requestvb,
					 pi->type, ptr, len);
		break;
	default:
		snmp_log(LOG_ERR, "SNMP: " SL_ER
			"unknown mode (%d) in "GT_GROUP_NAME"\n",
			reqinfo->mode);
		return SNMP_ERR_GENERR;
	}
	return SNMP_ERR_NOERROR;
}

/* init function */
void GT_INIT_FUNC(void)
{
	const oid wrsGT_oid[] = { GT_OID };
	netsnmp_handler_registration *hreg;

	/* do the registration for the scalars/globals */
	hreg = netsnmp_create_handler_registration(
		GT_GROUP_NAME, group_handler,
		wrsGT_oid, OID_LENGTH(wrsGT_oid),
		HANDLER_CAN_RONLY);
	netsnmp_register_scalar_group(
		hreg, 1 /* min */, ARRAY_SIZE(GT_PICKINFO) /* max */);
}

/*  be safe, in case some other header had them slightly differently */
#undef container_of
#undef offsetof
#undef ARRAY_SIZE

#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})

#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* the macro below relies on an externally-defined structure type */
#define DUMP_FIELD(_type, _fname) { \
	.name = #_fname ":",  \
	.type = dump_type_ ## _type, \
	.offset = offsetof(DUMP_STRUCT, _fname), \
}
#define DUMP_FIELD_SIZE(_type, _fname, _size) { \
	.name = #_fname ":",		\
	.type = dump_type_ ## _type, \
	.offset = offsetof(DUMP_STRUCT, _fname), \
	.size = _size, \
}

/*
 * To ease copying from header files, allow int, char and other known types.
 * Please add more type as more structures are included here
 */
enum dump_type {
	dump_type_char, /* for zero-terminated strings */
	dump_type_char_e, /* for zero-terminated strings with thw wrong
			   * endianess */
	dump_type_bina, /* for binary stull in MAC format */
	/* normal types follow */
	dump_type_uint64_t,
	dump_type_uint32_t,
	dump_type_uint16_t,
	dump_type_int,
	dump_type_unsigned_long,
	dump_type_unsigned_char,
	dump_type_unsigned_short,
	dump_type_double,
	dump_type_float,
	dump_type_pointer,
	/* and strange ones, from IEEE */
	dump_type_UInteger64,
	dump_type_Integer64,
	dump_type_UInteger32,
	dump_type_Integer32,
	dump_type_UInteger16,
	dump_type_Integer16,
	dump_type_UInteger8,
	dump_type_Integer8,
	dump_type_Enumeration8,
	dump_type_Boolean,
	dump_type_ClockIdentity,
	dump_type_PortIdentity,
	dump_type_ClockQuality,
	/* and this is ours */
	dump_type_time,
	dump_type_ip_address,
	dump_type_sfp_flags,
	dump_type_port_mode,
	dump_type_sensor_temp,
	/* SoftPLL's enumerations */
	dump_type_spll_mode,
	dump_type_spll_seq_state,
	dump_type_spll_align_state,
	/* rtu_filtering_entry enumerations */
	dump_type_rtu_filtering_entry_dynamic,
	dump_type_array_int,
};
/*
 * A structure to dump fields. This is meant to simplify things, see use here
 */
struct dump_info {
	char *name;
	enum dump_type type;   /* see above */
	int offset;
	int size;  /* only for strings or binary strings */
};

void dump_many_fields(void *addr, struct dump_info *info, int ninfo);
int dump_ppsi_mem(struct wrs_shm_head *head);

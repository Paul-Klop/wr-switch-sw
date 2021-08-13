#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <libwr/shmem.h>
#include <libwr/hal_shmem.h>
#include <libwr/rtu_shmem.h>
#include <libwr/softpll_export.h>
#include <libwr/util.h>
#include <ppsi/ppsi.h>
#include <ppsi-wrs.h>
#include "time_lib.h"

/*  be safe, in case some other header had them slightly differently */
#undef container_of
#undef offsetof
#undef ARRAY_SIZE

#include "wrs_dump_shmem.h"

#define FPGA_SPLL_STAT 0x10006800
#define SPLL_MAGIC 0x5b1157a7

void dump_one_field_ppsi_wrs(int type, int size, void *p, int i);
int dump_one_field_type_ppsi_wrs(int type, int size, void *p);

char *name_id_to_name[WRS_SHM_N_NAMES] = {
	[wrs_shm_ptp] = "ptpd/ppsi",
	[wrs_shm_rtu] = "wrsw_rtud",
	[wrs_shm_hal] = "wrsw_hal",
	[wrs_shm_vlan] = "wrs_vlans",
};

/* index of a the greatest number describing the SPLL mode +1 */
#define SPLL_MODE_MAX_N 5
char *spll_mode_to_name[SPLL_MODE_MAX_N] = {
	[SPLL_MODE_GRAND_MASTER] = "Grand Master",
	[SPLL_MODE_FREE_RUNNING_MASTER] = "Free Runnig Master",
	[SPLL_MODE_SLAVE] = "Slave",
	[SPLL_MODE_DISABLED] = "Disabled",
};

/* index of a the greatest number describing the SPLL sequence +1 */
#define SPLL_SEQ_STATE_MAX_N 11
char *spll_seq_state_to_name[SPLL_SEQ_STATE_MAX_N] = {
	[SEQ_START_EXT] = "start ext",
	[SEQ_WAIT_EXT] = "wait ext",
	[SEQ_START_HELPER] = "start helper",
	[SEQ_WAIT_HELPER] = "wait helper",
	[SEQ_START_MAIN] = "start main",
	[SEQ_WAIT_MAIN] = "wait main",
	[SEQ_DISABLED] = "disabled",
	[SEQ_READY] = "ready",
	[SEQ_CLEAR_DACS] = "clear dacs",
	[SEQ_WAIT_CLEAR_DACS] = "wait clear dacs",
};

/* index of a the greatest number describing the SPLL align state +1 */
#define SPLL_ALIGN_STATE_MAX_N 11
char *spll_align_state_to_name[SPLL_ALIGN_STATE_MAX_N] = {
	[ALIGN_STATE_EXT_OFF] = "ext off",
	[ALIGN_STATE_START] = "start",
	[ALIGN_STATE_INIT_CSYNC] = "init csync",
	[ALIGN_STATE_WAIT_CSYNC] = "wait csync",
	[ALIGN_STATE_WAIT_SAMPLE] = "wait sample",
	[ALIGN_STATE_COMPENSATE_DELAY] = "compensate delay",
	[ALIGN_STATE_LOCKED] = "locked",
	[ALIGN_STATE_START_ALIGNMENT] = "start alignment",
	[ALIGN_STATE_START_MAIN] = "start main",
	[ALIGN_STATE_WAIT_CLKIN] = "wait clkIn",
	[ALIGN_STATE_WAIT_PLOCK] = "wait plock",
};

/* index of a the greatest number describing the qmode +1 */
#define RTU_QMODE_MAX 5
char *rtu_qmode_to_name[RTU_QMODE_MAX] = {
	[QMODE_ACCESS] =   "access",
	[QMODE_TRUNK] =    "trunk",
	[QMODE_DISABLED] = "disabled",
	[QMODE_UNQ] =      "unqualified",
	[QMODE_INVALID] =  "invalid",
};

static int dump_all_rtu_entries = 0; /* rtu exports 4096 vlans and 2048 htab
				 entries */


#define REL_DIFF_FRACBITS 62
#define REL_DIFF_FRACMASK 0x3fffffffffffffff

void decode_relative_difference(RelativeDifference rd, int32_t *nsecs, uint64_t *sub_yocto) {
    int64_t fraction;
	uint64_t bitWeight=500000000000000000;
	uint64_t mask;

	*sub_yocto=0;
	*nsecs = (int32_t)(rd >> REL_DIFF_FRACBITS);
    fraction=(int64_t)rd & REL_DIFF_FRACMASK;
	for (mask=(uint64_t) 1<< (REL_DIFF_FRACBITS-1);mask!=0; mask>>=1 ) {
		if ( mask & fraction )
			*sub_yocto+=bitWeight;
		bitWeight/=2;
	}
}

void dump_one_field(void *addr, struct dump_info *info, char *info_prefix)
{
	void *p = addr + info->offset;
	char buf[128];
	struct pp_time *t = p;
	char format[16];
	int i;
	int value;
	char pname[128];

	if (info_prefix!=NULL )
		sprintf(pname,"%s.%s",info_prefix,info->name);
	else
		strcpy(pname,info->name);

	/* For some (mostly enum-like types) the size may vary.
	 * Check the size and assign a proper value to
	 * variable i */
	switch(info->type) {
	case dump_type_yes_no:
		if (info->size == 1)
			value = *(uint8_t *)p;
		else if (info->size == 2)
			value = *(uint16_t *)(p);
		else
			value = *(uint32_t *)(p);
		break;
	default:
		/* check if this is ppsi type */
		value = dump_one_field_type_ppsi_wrs(info->type, info->size, p);
	}

	printf("%-40s ", pname); /* name includes trailing ':' */
	switch(info->type) {
	case dump_type_char:
		sprintf(format,"\"%%.%is\"\n", info->size);
		printf(format, (char *)p);
		break;
	case dump_type_char_e: /* swap endianess */
		i = info->size;
		printf("\"");
		while (i > 0) {
			strncpy_e(format, (char *) p, 1);
			printf("%.4s", (char *)format);
			i -= 4;
			p = ((char *) p) + 4;
		}
		printf("\"\n");
		break;
	case dump_type_bina:
		for (i = 0; i < info->size; i++)
			printf("%02x%c", ((unsigned char *)p)[i],
			       i == info->size - 1 ? '\n' : ':');
		break;
	case dump_type_uint64_t:
		printf("%lld\n", *(unsigned long long *)p);
		break;
	case dump_type_long_long:
		printf("%lld\n", *(long long *)p);
		break;
	case dump_type_uint32_t:
		printf("0x%08lx\n", (long)*(uint32_t *)p);
		break;
	case dump_type_int:
		printf("%i\n", *(int *)p);
		break;
	case dump_type_unsigned:
		printf("%u\n", *(uint32_t *)p);
		break;
	case dump_type_unsigned_long:
		printf("%lu\n", *(unsigned long *)p);
		break;
	case dump_type_unsigned_char:
		printf("%i\n", *(unsigned char *)p);
		break;
	case dump_type_uint16_t:
	case dump_type_unsigned_short:
		printf("%i\n", *(unsigned short *)p);
		break;
	case dump_type_double:
		printf("%lf\n", *(double *)p);
		break;
	case dump_type_float:
		printf("%f\n", *(float *)p);
		break;
	case dump_type_pointer:
		printf("%p\n", *(void **)p);
		break;
	case dump_type_yes_no:
		i = *(uint8_t *)p;
		switch (i) {
		case 0:
			printf("no(%d)\n", i);
			break;
		case 1:
			printf("yes(%d)\n", i);
			break;
		default:
			printf("Unknown(%d)\n", i);
		}
		break;

	case dump_type_timeval:
		{
		    struct timeval *tv = (struct timeval*) p;
		    printf("%ld.%06ld\n", tv->tv_sec, tv->tv_usec);
		    break;
		}

	case dump_type_time:
		printf("%s\n",timeToString(t,buf));
		break;

	case dump_type_ip_address:
		for (i = 0; i < 4; i++)
			printf("%02x%c", ((unsigned char *)p)[i],
			       i == 3 ? '\n' : ':');
		break;

	case dump_type_sfp_flags:
		if (*(uint32_t *)p & SFP_FLAG_CLASS_DATA)
			printf("SFP class data, ");
		if (*(uint32_t *)p & SFP_FLAG_DEVICE_DATA)
			printf("SFP device data, ");
		if (*(uint32_t *)p & SFP_FLAG_1GbE)
			printf("SFP is 1GbE, ");
		if (*(uint32_t *)p & SFP_FLAG_IN_DB)
			printf("SFP in data base, ");
		printf("\n");
		break;
	case dump_type_sfp_dom_temp:
		printf("%.3f C\n", ntohs(*(short *)p)/(float)256);
		break;
	case dump_type_sfp_dom_voltage:
		printf("%.3f V\n", ntohs(*(short *)p)/(float)10000);
		break;
	case dump_type_sfp_dom_bias_curr:
		printf("%.3f mA\n", ntohs(*(short *)p)/(float)500);
		break;
	case dump_type_sfp_dom_tx_power:
	case dump_type_sfp_dom_rx_power:
		printf("%.3f mW\n", ntohs(*(short *)p)/(float)10000);
		break;
	case dump_type_port_mode:
		switch (*(uint32_t *)p) {
		case HEXP_PORT_MODE_WR_MASTER:
			printf("WR Master\n");
			break;
		case HEXP_PORT_MODE_WR_SLAVE:
			printf("WR Slave\n");
			break;
		case HEXP_PORT_MODE_NON_WR:
			printf("Non-WR\n");
			break;
		case HEXP_PORT_MODE_NONE:
			printf("None\n");
			break;
		case HEXP_PORT_MODE_WR_M_AND_S:
			printf("Auto\n");
			break;
		default:
			printf("Undefined\n");
			break;
		}
		break;
	case dump_type_sensor_temp:
		printf("%f\n", ((float)(*(int *)p >> 4)) / 16.0);
		break;
	case dump_type_spll_mode:
		i = *(uint32_t *)p;
		switch (i) {
		case SPLL_MODE_GRAND_MASTER:
		case SPLL_MODE_FREE_RUNNING_MASTER:
		case SPLL_MODE_SLAVE:
		case SPLL_MODE_DISABLED:
			printf("%s(%d)\n", spll_mode_to_name[i], i);
			break;
		default:
			printf("Unknown(%d)\n", i);
		}
		break;
	case dump_type_spll_seq_state:
		i = *(uint32_t *)p;
		switch (i) {
		case SEQ_START_EXT:
		case SEQ_WAIT_EXT:
		case SEQ_START_HELPER:
		case SEQ_WAIT_HELPER:
		case SEQ_START_MAIN:
		case SEQ_WAIT_MAIN:
		case SEQ_DISABLED:
		case SEQ_READY:
		case SEQ_CLEAR_DACS:
		case SEQ_WAIT_CLEAR_DACS:
			printf("%s(%d)\n", spll_seq_state_to_name[i], i);
			break;
		default:
			printf("Unknown(%d)\n", i);
		}
		break;
	case dump_type_spll_align_state:
		i = *(uint32_t *)p;
		switch (i) {
		case ALIGN_STATE_EXT_OFF:
		case ALIGN_STATE_START:
		case ALIGN_STATE_INIT_CSYNC:
		case ALIGN_STATE_WAIT_CSYNC:
		case ALIGN_STATE_WAIT_SAMPLE:
		case ALIGN_STATE_COMPENSATE_DELAY:
		case ALIGN_STATE_LOCKED:
		case ALIGN_STATE_START_ALIGNMENT:
		case ALIGN_STATE_START_MAIN:
		case ALIGN_STATE_WAIT_CLKIN:
		case ALIGN_STATE_WAIT_PLOCK:
			printf("%s(%d)\n", spll_align_state_to_name[i], i);
			break;
		default:
			printf("Unknown(%d)\n", i);
		}
		break;
	case dump_type_rtu_filtering_entry_dynamic:
		i = *(uint32_t *)p;
		switch (i) {
		case RTU_ENTRY_TYPE_STATIC:
			printf("static\n");
			break;
		case RTU_ENTRY_TYPE_DYNAMIC:
			printf("dynamic\n");
			break;
		default:
			printf("Unknown(%d)\n", i);
		}
		break;
        case dump_type_rtu_qmode:
		i = *(uint32_t *)p;
		switch (i) {
		case QMODE_ACCESS:
		case QMODE_TRUNK:
		case QMODE_DISABLED:
		case QMODE_UNQ:
		case QMODE_INVALID:
			printf("%s(%d)\n", rtu_qmode_to_name[i], i);
			break;
		default:
			printf("Unknown(%d)\n", i);
		}
		break;
	case dump_type_array_int:
		{
		int *size = addr + info->size;
		for (i = 0; i < *size; i++)
			printf("%d ", *((int *)p + i));
		printf("\n");
		break;
		}

	default:
		dump_one_field_ppsi_wrs(info->type, info->size, p, value);
		break;
	}
}

void dump_many_fields(void *addr, struct dump_info *info, int ninfo, char *prefix)
{
	int i;

	if (!addr) {
		fprintf(stderr, "dump: pointer not valid\n");
		return;
	}
	for (i = 0; i < ninfo; i++) {
		dump_one_field(addr, info + i,prefix);
	}
}

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

#undef DUMP_STRUCT
#define DUMP_STRUCT struct hal_shmem_header
struct dump_info hal_shmem_info [] = {
	DUMP_FIELD(int, nports),
	DUMP_FIELD(int, shmemState),
	DUMP_FIELD(int, hal_mode),
	DUMP_FIELD(int, read_sfp_diag),
	DUMP_FIELD(sensor_temp, temp.fpga),
	DUMP_FIELD(sensor_temp, temp.pll),
	DUMP_FIELD(sensor_temp, temp.psl),
	DUMP_FIELD(sensor_temp, temp.psr),
};

/* map for fields of hal_port_state (hal_shmem.h) */
#undef DUMP_STRUCT
#define DUMP_STRUCT struct hal_port_state
struct dump_info hal_port_info [] = {
	DUMP_FIELD(int, in_use),
	DUMP_FIELD_SIZE(char, name, 16),
	DUMP_FIELD_SIZE(bina, hw_addr, 6),
	DUMP_FIELD(int, hw_index),
	DUMP_FIELD(int, fd),
	DUMP_FIELD(int, hw_addr_auto),
	DUMP_FIELD(int, fsm.st.state),
	DUMP_FIELD(int, pllFsm.st.state),
	DUMP_FIELD(int, fiber_index),
	DUMP_FIELD(int, locked),
	/* these fields are defined as uint32_t but we prefer %i to %x */
	DUMP_FIELD(int, calib.phy_rx_min),
	DUMP_FIELD(int, calib.phy_tx_min),
	DUMP_FIELD(int, calib.delta_tx_phy),
	DUMP_FIELD(int, calib.delta_rx_phy),
	DUMP_FIELD(int, calib.delta_tx_board),
	DUMP_FIELD(int, calib.delta_rx_board),
	DUMP_FIELD(int, calib.rx_calibrated),
	DUMP_FIELD(int, calib.tx_calibrated),
	DUMP_FIELD(int, calib.bitslide_ps),


	/* Another internal structure, with a final pointer */
	DUMP_FIELD(sfp_flags, calib.sfp.flags),
	DUMP_FIELD_SIZE(char, calib.sfp.vendor_name, 16),
	DUMP_FIELD_SIZE(char, calib.sfp.part_num, 16),
	DUMP_FIELD_SIZE(char, calib.sfp.vendor_serial, 16),
	DUMP_FIELD(double,    calib.sfp.alpha),
	DUMP_FIELD(int,       calib.sfp.delta_tx_ps),
	DUMP_FIELD(int,       calib.sfp.delta_rx_ps),
	DUMP_FIELD(int,       calib.sfp.tx_wl),
	DUMP_FIELD(int,       calib.sfp.rx_wl),
	DUMP_FIELD(pointer,   calib.sfp.next),

	/* Dump some values from the SFP's DOM area of */
	DUMP_FIELD(sfp_dom_temp,      calib.sfp_dom_raw.temp),
	DUMP_FIELD(sfp_dom_voltage,   calib.sfp_dom_raw.vcc),
	DUMP_FIELD(sfp_dom_bias_curr, calib.sfp_dom_raw.tx_bias),
	DUMP_FIELD(sfp_dom_tx_power,  calib.sfp_dom_raw.tx_pow),
	DUMP_FIELD(sfp_dom_rx_power,  calib.sfp_dom_raw.rx_pow),

	DUMP_FIELD(uint32_t, phase_val),
	DUMP_FIELD(int, phase_val_valid),
	DUMP_FIELD(int, tx_cal_pending),
	DUMP_FIELD(int, rx_cal_pending),
	DUMP_FIELD(int, lock_state),
	DUMP_FIELD(uint32_t, clock_period),
	DUMP_FIELD(uint32_t, t2_phase_transition),
	DUMP_FIELD(uint32_t, t4_phase_transition),
	DUMP_FIELD(int, t24p_from_config),
	DUMP_FIELD(uint32_t, ep_base),
	DUMP_FIELD(int, sfpPresent),
	DUMP_FIELD(yes_no, has_sfp_diag),
	DUMP_FIELD(yes_no, monitor),

	/* PPSi instance information */
	DUMP_FIELD(int, portMode),
	DUMP_FIELD(int, synchronized),
	DUMP_FIELD(int, portInfoUpdated),

	/* Events to process */
	DUMP_FIELD(int,  evt_reset),
	DUMP_FIELD(int,  evt_lock),
	DUMP_FIELD(int,  evt_linkUp),

};

/* map for fields of hal_port_state.lpdc (hal_shmem.h) */
#undef DUMP_STRUCT
#define DUMP_STRUCT halPortLPDC_t
struct dump_info hal_port_info_lpdc [] = {
		DUMP_FIELD(int, isSupported),
		DUMP_FIELD(int, txSetupFSM.st.state),
		DUMP_FIELD(int, rxSetupFSM.st.state),
};

/* map for fields of hal_port_state.lpdc.txsetup (hal_shmem.h) */
#undef DUMP_STRUCT
#define DUMP_STRUCT halPortLpdcTx_t
struct dump_info hal_port_info_lpdc_txsetup [] = {
		DUMP_FIELD(int, attempts),
		DUMP_FIELD(int, cal_saved_phase),
		DUMP_FIELD(int, cal_saved_phase_valid),
		DUMP_FIELD(int, measured_phase),
		DUMP_FIELD(int, expected_phase),
		DUMP_FIELD(int, tolerance),
		DUMP_FIELD(int, update_cnt),
		DUMP_FIELD(int, expected_phase_valid),
};

/* map for fields of hal_port_state.lpdc.rxsetup (hal_shmem.h) */
#undef DUMP_STRUCT
#define DUMP_STRUCT halPortLpdcRx_t
struct dump_info hal_port_info_lpdc_rxsetup [] = {
		DUMP_FIELD(int, attempts),
};

/* Generic share memory head */
#undef DUMP_STRUCT
#define DUMP_STRUCT struct wrs_shm_head
struct dump_info shm_head [] = {
		DUMP_FIELD(int, version),
		DUMP_FIELD(unsigned, pidsequence),
		DUMP_FIELD(unsigned, pid),
		DUMP_FIELD(unsigned, data_size),
		DUMP_FIELD(unsigned_long, stamp)
};

int dump_hal_mem(struct wrs_shm_head *head)
{
	struct hal_shmem_header *h;
	struct hal_port_state *p;
	int i, n;

	if (head->version != HAL_SHMEM_VERSION) {
		fprintf(stderr, "dump hal: unknown version %i (known is %i)\n",
			head->version, HAL_SHMEM_VERSION);
		return -1;
	}
	h = (void *)head + head->data_off;

	/* dump shmem header*/
	dump_many_fields(head, shm_head, ARRAY_SIZE(shm_head),"HAL.shm");

	/* dump hal's shmem */
	dump_many_fields(h, hal_shmem_info, ARRAY_SIZE(hal_shmem_info),"HAL");

	n = h->nports;
	p = wrs_shm_follow(head, h->ports);

	if (!p) {
		fprintf(stderr, "dump hal: cannot follow pointer to *ports\n");
		return -1;
	}

	for (i = 0; i < n; i++, p++) {
		char prefix[64];
		char prefix2[64];


		sprintf(prefix,"HAL.port.%d",i+1);
		dump_many_fields(p, hal_port_info, ARRAY_SIZE(hal_port_info),prefix);
		strcat(prefix,".lpdc");
		dump_many_fields(&p->lpdc, hal_port_info_lpdc, ARRAY_SIZE(hal_port_info_lpdc),prefix);
		if ( p->lpdc.txSetup) {
			halPortLpdcTx_t *txsetup;

			if ( (txsetup=wrs_shm_follow(head, p->lpdc.txSetup))!=NULL ) {
				strcpy(prefix2,prefix);
				strcat(prefix2,".txsetup");
				dump_many_fields(txsetup, hal_port_info_lpdc_txsetup, ARRAY_SIZE(hal_port_info_lpdc_txsetup),prefix2);
			}
		}
		if ( p->lpdc.rxSetup) {
			halPortLpdcTx_t *rxsetup;

			if ( (rxsetup=wrs_shm_follow(head, p->lpdc.rxSetup))!=NULL ) {
				strcpy(prefix2,prefix);
				strcat(prefix2,".rxsetup");
				dump_many_fields(rxsetup, hal_port_info_lpdc_rxsetup, ARRAY_SIZE(hal_port_info_lpdc_rxsetup),prefix2);
			}
		}
	}
	return 0;
}

/* map for fields of rtud structures */
#undef DUMP_STRUCT
#define DUMP_STRUCT struct rtu_filtering_entry
struct dump_info htab_info[] = {
	DUMP_FIELD(int, addr.hash),
	DUMP_FIELD(int, addr.bucket),
	DUMP_FIELD(yes_no, valid),
	DUMP_FIELD(int, end_of_bucket),
	DUMP_FIELD(yes_no, is_bpdu),
	DUMP_FIELD_SIZE(bina, mac, ETH_ALEN),
	DUMP_FIELD(unsigned_char, fid),
	DUMP_FIELD(uint32_t, port_mask_src),
	DUMP_FIELD(uint32_t, port_mask_dst),
	DUMP_FIELD(int, drop_when_source),
	DUMP_FIELD(int, drop_when_dest),
	DUMP_FIELD(int, drop_unmatched_src_ports),
	DUMP_FIELD(unsigned, last_access_t),
	DUMP_FIELD(yes_no, force_remove),
	DUMP_FIELD(unsigned_char, prio_src),
	DUMP_FIELD(int, has_prio_src),
	DUMP_FIELD(int, prio_override_src),
	DUMP_FIELD(unsigned_char, prio_dst),
	DUMP_FIELD(int, has_prio_dst),
	DUMP_FIELD(int, prio_override_dst),
	DUMP_FIELD(rtu_filtering_entry_dynamic, dynamic),
	DUMP_FIELD(int, age),
};

#undef DUMP_STRUCT
#define DUMP_STRUCT struct rtu_vlan_table_entry
struct dump_info vlan_info[] = {
	DUMP_FIELD(uint32_t, port_mask),
	DUMP_FIELD(unsigned_char, fid),
	DUMP_FIELD(unsigned_char, prio),
	DUMP_FIELD(yes_no, has_prio),
	DUMP_FIELD(yes_no, prio_override),
	DUMP_FIELD(yes_no, drop),
	DUMP_FIELD(timeval, creation_time),
};

#undef DUMP_STRUCT
#define DUMP_STRUCT struct rtu_mirror_info
struct dump_info mirror_info[] = {
	DUMP_FIELD(yes_no, en),
	DUMP_FIELD(uint32_t, imask),
	DUMP_FIELD(uint32_t, emask),
	DUMP_FIELD(uint32_t, dmask),
};

#undef DUMP_STRUCT
#define DUMP_STRUCT struct rtu_port_entry
struct dump_info rtu_port_info[] = {
	DUMP_FIELD(rtu_qmode, qmode),
	DUMP_FIELD(yes_no, fix_prio),
	DUMP_FIELD(unsigned_char, prio),
	DUMP_FIELD(uint16_t, pvid),
	DUMP_FIELD_SIZE(bina, mac, ETH_ALEN),
	DUMP_FIELD(yes_no, untag),
};

int dump_rtu_mem(struct wrs_shm_head *head)
{
	struct rtu_shmem_header *rtu_h;
	struct rtu_filtering_entry *rtu_filters;
	struct rtu_filtering_entry *rtu_filters_cur;
	struct rtu_vlan_table_entry *rtu_vlans;
	struct rtu_mirror_info *rtu_mirror;
	struct rtu_port_entry *rtu_ports;
	int i, j;
	int nports;
	char prefix[64];

	if (head->version != RTU_SHMEM_VERSION) {
		fprintf(stderr, "dump rtu: unknown version %i (known is %i)\n",
			head->version, RTU_SHMEM_VERSION);
		return -1;
	}
	rtu_h = (void *)head + head->data_off;
	rtu_filters = wrs_shm_follow(head, rtu_h->filters);
	rtu_vlans = wrs_shm_follow(head, rtu_h->vlans);
	rtu_mirror = wrs_shm_follow(head, rtu_h->mirror);
	rtu_ports = wrs_shm_follow(head, rtu_h->rtu_ports);

	if ((!rtu_filters) || (!rtu_vlans) || (!rtu_mirror)) {
		fprintf(stderr, "dump rtu: cannot follow pointer in shm\n");
		return -1;
	}

	/* get number of ports from rtu */
	nports = rtu_h->rtu_nports;
	if (nports <= 0) {
		fprintf(stderr, "dump rtu: unable to get number of ports\n");
		return -1;
	}

	/* dump shmem header*/
	dump_many_fields(head, shm_head, ARRAY_SIZE(shm_head),"rtu.shm");

	for (i = 0; i < HTAB_ENTRIES; i++) {
		for (j = 0; j < RTU_BUCKETS; j++) {
			rtu_filters_cur = rtu_filters + i*RTU_BUCKETS + j;
			if ((!dump_all_rtu_entries)
			    && (!rtu_filters_cur->valid))
				/* don't display empty entries */
				continue;
			sprintf(prefix,"rtu.htab.%d.%d",i,j);
			dump_many_fields(rtu_filters_cur, htab_info,
					 ARRAY_SIZE(htab_info),prefix);
		}
	}

	for (i = 0; i < NUM_VLANS; i++, rtu_vlans++) {
		if ((!dump_all_rtu_entries) && (rtu_vlans->drop != 0
			    && rtu_vlans->port_mask == 0x0))
			/* don't display empty entries */
			continue;
		sprintf(prefix,"rtu.vlan.%d",i);
		dump_many_fields(rtu_vlans, vlan_info, ARRAY_SIZE(vlan_info),prefix);
	}

	sprintf(prefix, "rtu.mirror");
	dump_many_fields(rtu_mirror, mirror_info, ARRAY_SIZE(mirror_info),
			prefix);

	for (i = 0; i < nports; i++, rtu_ports++) {
		sprintf(prefix,"rtu.ports.%d", i + 1);
		dump_many_fields(rtu_ports, rtu_port_info,
				 ARRAY_SIZE(rtu_port_info), prefix);
	}

	return 0;
}

#undef DUMP_STRUCT
#define DUMP_STRUCT struct spll_stats
struct dump_info spll_stats_info[] = {
	DUMP_FIELD(uint32_t, magic),	/* 0x5b1157a7 = SPLLSTAT ?;)*/
	DUMP_FIELD(int, ver),
	DUMP_FIELD(int, sequence),
	DUMP_FIELD(spll_mode, mode),
	DUMP_FIELD(int, irq_cnt),
	DUMP_FIELD(spll_seq_state, seq_state),
	DUMP_FIELD(spll_align_state, align_state),
	DUMP_FIELD(int, H_lock),
	DUMP_FIELD(int, M_lock),
	DUMP_FIELD(int, H_y),
	DUMP_FIELD(int, M_y),
	DUMP_FIELD(int, del_cnt),
	DUMP_FIELD(int, start_cnt),
	DUMP_FIELD_SIZE(char_e, commit_id, 32),
	DUMP_FIELD_SIZE(char_e, build_date, 16),
	DUMP_FIELD_SIZE(char_e, build_time, 16),
	DUMP_FIELD_SIZE(char_e, build_by, 32),
};

static int dump_spll_mem(struct spll_stats *spll)
{
	/* Check magic */
	if (spll->magic != SPLL_MAGIC) {
		/* Wrong magic */
		fprintf(stderr, "dump spll: unknown magic %x (known is %x)\n",
			spll->magic, SPLL_MAGIC);
	}

	dump_many_fields(spll, spll_stats_info, ARRAY_SIZE(spll_stats_info),"SoftPll");

	return 0; /* this is complete */
}

int dump_any_mem(struct wrs_shm_head *head)
{
	unsigned char *p = (void *)head;
	int i, j;

	p += head->data_off;
	for (i = 0; i < head->data_size; i++) {
		if (i % 16 == 0)
			printf("%04lx: ", i + head->data_off);
		printf("%02x ", p[i]);
		if (i % 16 == 15) { /* ascii row */
			printf("    ");
			for (j = i & ~15; j <= i; j++)
				printf("%c", p[j] >= 0x20 && p[j] < 0x7f
				       ? p[j] : '.');
			printf("\n");
		}
	}
	/* yes, last row has no ascii trailer. Who cares */
	if (head ->data_size ^ 0xf)
		printf("\n");
	return 0;
}

typedef int (dump_f)(struct wrs_shm_head *head);

dump_f *name_id_to_f[WRS_SHM_N_NAMES] = {
	[wrs_shm_hal] = dump_hal_mem,
	[wrs_shm_ptp] = dump_ppsi_mem,
	[wrs_shm_rtu] = dump_rtu_mem,
};

void print_info(char *prgname)
{
	printf("usage: %s [parameters]\n", prgname);
	printf(""
		"             Dump shmem\n"
		"   -a        Dump all rtu entries. By default only valid\n"
		"             entries are printed. Note there are 2048 htab\n"
		"             and 4096 vlan entries!\n"
		"   -H <dir>  Open shmem dumps from the given directory\n"
		"   -h        Show this message\n"
		"  Dump shmem for specific program (by default dump for all)\n"
		"   -P        Dump ptp entries\n"
		"   -R        Dump rtu entries\n"
		"   -L        Dump hal entries\n"
		"   -S        Dump SoftPll entries\n");

}

static int dump_print[WRS_SHM_N_NAMES];
static int dump_spll = 0;

int main(int argc, char **argv)
{
	struct wrs_shm_head *head;
	dump_f *f;
	void *m;
	int i;
	int c;
	int print_all = 1;
	struct spll_stats *spll_stats_p;

	while ((c = getopt(argc, argv, "ahH:PRLS")) != -1) {
		switch (c) {
		case 'a':
			dump_all_rtu_entries = 1;
			break;
		case 'H':
			wrs_shm_set_path(optarg);
			break;
		case 'P':
			dump_print[wrs_shm_ptp] = 1;
			print_all = 0;
			break;
		case 'R':
			dump_print[wrs_shm_rtu] = 1;
			print_all = 0;
			break;
		case 'L':
			dump_print[wrs_shm_hal] = 1;
			print_all = 0;
			break;
		case 'S':
			dump_spll = 1;
			print_all = 0;
			break;
		case 'h':
		default:
			print_info(argv[0]);
			exit(1);
		}
	}
	for (i = 0; i < WRS_SHM_N_NAMES; i++) {
		if (!print_all && !dump_print[i])
			continue;

		m = wrs_shm_get(i, "reader", 0);
		if (!m) {
			fprintf(stderr, "%s: can't attach memory area %i: %s\n",
				argv[0], i, strerror(errno));
			continue;
		}
		head = m;
		if (!head->pidsequence) {
			printf("shm.%d.name:       %s\n",i,name_id_to_name[i]);
			printf("shm.%d.iterations: %d  (no data)\n",i,head->pidsequence);
			wrs_shm_put(m);
			continue;
		}
		printf("shm.%d.name:       %s\n",i,head->name);
		printf("shm.%d.pid:        %d\n",i,head->pid);
		if (head->pid) {
			printf("shm.%d.status:     %s\n",i,kill(head->pid, 0) < 0 ? "dead" : "alive");
		}
		printf("shm.%d.iterations: %d\n",i,head->pidsequence);
		printf("shm.%d.mapbase:    %p\n", i, head->mapbase);
		f = name_id_to_f[i];

		/* if the area-specific function fails, fall back to generic */
		if (!f || f(head) != 0)
			dump_any_mem(head);
		wrs_shm_put(m);
		printf("\n"); /* separate one area from the next */
	}
	if (print_all || dump_spll) {
		spll_stats_p = create_map(FPGA_SPLL_STAT,
					  sizeof(*spll_stats_p));
		dump_spll_mem(spll_stats_p);
	}
	return 0;
}

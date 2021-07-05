#include "wrsSnmp.h"
#include "snmp_shmem.h"

/* HAL */
struct wrs_shm_head *hal_head;
struct hal_shmem_header *hal_shmem;
struct hal_port_state *hal_ports;
int hal_nports_local;

/* PPSI */
struct wrs_shm_head *ppsi_head;
static struct pp_globals *ppg;
struct pp_instance *ppsi_ppi;
parentDS_t *ppsi_parentDS;
defaultDS_t *ppsi_defaultDS;

int *ppsi_ppi_nlinks=NULL;

/* RTUd */
struct wrs_shm_head *rtud_head;

int shmem_open_hald;
int shmem_open_ppsi;
int shmem_open_rtud;

static int init_shm_hald(void)
{
	int ret;
	static int n_wait = 0;

	ret = wrs_shm_get_and_check(wrs_shm_hal, &hal_head);
	n_wait++;
	/* start printing error after 5 messages */
	if (n_wait > 5) {
		if (ret == WRS_SHM_OPEN_FAILED) {
			snmp_log(LOG_ERR, "SNMP: " SL_ER
				"Unable to open HAL's shmem!\n");
		}
		if (ret == WRS_SHM_WRONG_VERSION) {
			snmp_log(LOG_ERR, "SNMP: " SL_ER
				"Unable to read HAL's version!\n");
		}
		if (ret == WRS_SHM_INCONSISTENT_DATA) {
			snmp_log(LOG_ERR, "SNMP: " SL_ER
				"Unable to read consistent data from"
				 " HAL's shmem!\n");
		}
	}
	if (ret) {
		/* return if error while opening shmem */
		return ret;
	}

	/* check hal's shm version */
	if (hal_head->version != HAL_SHMEM_VERSION) {
		snmp_log(LOG_ERR, "SNMP: " SL_ER
                        "unknown hal's shm version %i "
			 "(known is %i)\n", hal_head->version,
			 HAL_SHMEM_VERSION);
		return 3;
	}

	hal_shmem = (void *)hal_head + hal_head->data_off;
	/* Assume number of ports does not change in runtime */
	hal_nports_local = hal_shmem->nports;
	if (hal_nports_local > WRS_N_PORTS) {
		snmp_log(LOG_ERR, "SNMP: " SL_ER
			"Too many ports reported by HAL. "
			"%d vs %d supported\n",
			hal_nports_local, WRS_N_PORTS);
		return 3;
	}
	/* Even after HAL restart, HAL will place structures at the same
	 * addresses. No need to re-dereference pointer at each read. */
	hal_ports = wrs_shm_follow(hal_head, hal_shmem->ports);
	if (!hal_ports) {
		snmp_log(LOG_ERR, "SNMP: " SL_ER
			"Unalbe to follow hal_ports pointer in HAL's"
			 " shmem");
		return 3;
	}

	/* everything is ok */
	return 0;
}

static int init_shm_ppsi(void)
{
	int ret;
	int n_wait = 0;

	ret = wrs_shm_get_and_check(wrs_shm_ptp, &ppsi_head);
	n_wait++;
	/* start printing error after 5 messages */
	if (n_wait > 5) {
		/* timeout! */
		if (ret == WRS_SHM_OPEN_FAILED) {
			snmp_log(LOG_ERR, "SNMP: " SL_ER
				 "Unable to open shm for PPSI!\n");
		}
		if (ret == WRS_SHM_WRONG_VERSION) {
			snmp_log(LOG_ERR, "SNMP: " SL_ER
				 "Unable to read PPSI's version!\n");
		}
		if (ret == WRS_SHM_INCONSISTENT_DATA) {
			snmp_log(LOG_ERR, "SNMP: " SL_ER
				 "Unable to read consistent data from"
				 " PPSI's shmem!\n");
		}
	}
	if (ret) {
		/* return if error while opening shmem */
		return ret;
	}

	/* check ppsi's shm version */
	if (ppsi_head->version != WRS_PPSI_SHMEM_VERSION) {
		snmp_log(LOG_ERR, "SNMP: " SL_ER
                        "unknown PPSI's shm version %i "
			"(known is %i)\n",
			ppsi_head->version, WRS_PPSI_SHMEM_VERSION);
		return 3;
	}
	ppg = (void *)ppsi_head + ppsi_head->data_off;

	ppsi_ppi = wrs_shm_follow(ppsi_head, ppg->pp_instances);
	if (!ppsi_ppi) {
		snmp_log(LOG_ERR, "SNMP: " SL_ER
			"Cannot follow ppsi_ppi in shmem.\n");
		return 5;
	}
	/* use pointer instead of copying */
	ppsi_ppi_nlinks = &(ppg->nlinks);

	ppsi_parentDS = wrs_shm_follow(ppsi_head, ppg->parentDS);
	if (!ppsi_parentDS) {
		snmp_log(LOG_ERR, "SNMP: " SL_ER
			"Cannot follow ppsi_parentDS in shmem.\n");
		return 5;
	}

	ppsi_defaultDS = wrs_shm_follow(ppsi_head, ppg->defaultDS);
	if (!ppsi_defaultDS) {
		snmp_log(LOG_ERR, "SNMP: " SL_ER
			"Cannot follow ppsi_defaultDS in shmem.\n");
		return 5;
	}
	return 0;
}

static int init_shm_rtud(void)
{
	int ret;
	static int n_wait = 0;

	ret = wrs_shm_get_and_check(wrs_shm_rtu, &rtud_head);
	n_wait++;
	/* start printing error after 5 messages */
	if (n_wait > 5) {
		if (ret == WRS_SHM_OPEN_FAILED) {
			snmp_log(LOG_ERR, "SNMP: " SL_ER
				 "Unable to open shm for RTUd!\n");
		}
		if (ret == WRS_SHM_WRONG_VERSION) {
			snmp_log(LOG_ERR, "SNMP: " SL_ER
				 "Unable to read RTUd's version!\n");
		}
		if (ret == WRS_SHM_INCONSISTENT_DATA) {
			snmp_log(LOG_ERR, "SNMP: " SL_ER
				 "Unable to read consistent data from"
				 " RTUd's shmem!\n");
		}
	}
	if (ret) {
		/* return if error while opening shmem */
		return ret;
	}

	/* check rtud's shm version */
	if (rtud_head->version != RTU_SHMEM_VERSION) {
		snmp_log(LOG_ERR, "SNMP: " SL_ER
			"unknown RTUd's shm version %i "
			 "(known is %i)\n", rtud_head->version,
			 RTU_SHMEM_VERSION);
		return 3;
	}

	/* everything is ok */
	return 0;
}

int shmem_ready_hald(void)
{
	if (shmem_open_hald) {
		return 1;
	}
	shmem_open_hald = !init_shm_hald();
	return shmem_open_hald;
}

int shmem_ready_ppsi(void)
{
	if (shmem_open_ppsi) {
		return 1;
	}
	shmem_open_ppsi = !init_shm_ppsi();
	return shmem_open_ppsi;
}

int shmem_ready_rtud(void)
{
	if (shmem_open_rtud) {
		return 1;
	}
	shmem_open_rtud = !init_shm_rtud();
	return shmem_open_rtud;
}

void init_shm(void){
	int i;
	char *shmem_path;

	/* If SHMEM_PATH is set in env variable, snmpd will use this path
	 * for shmem files */
	shmem_path = getenv("SHMEM_PATH");
	if (shmem_path) {
		wrs_shm_set_path(shmem_path);
		wrs_shm_ignore_flag_locked(1);
		snmp_log(LOG_WARNING, "SNMP: " SL_W
			"Using path %s for shmem!\n", shmem_path);
	}

	for (i = 0; i < 10; i++) {
		if (shmem_ready_hald()) {
			/* shmem opened successfully */
			break;
		}
		/* wait 1 second before another try */
		sleep(1);
	}
	for (i = 0; i < 10; i++) {
		if (shmem_ready_ppsi()) {
			/* shmem opened successfully */
			break;
		}
		/* wait 1 second before another try */
		sleep(1);
	}
	for (i = 0; i < 10; i++) {
		if (shmem_ready_rtud()) {
			/* shmem opened successfully */
			break;
		}
		/* wait 1 second before another try */
		sleep(1);
	}
}

int shmem_rtu_read_vlans(struct rtu_vlan_table_entry *vlan_tab_local)
{
	unsigned ii;
	unsigned retries = 0;
	struct rtu_vlan_table_entry *vlan_tab_shm;
	struct rtu_shmem_header *rtu_hdr;

	rtu_hdr = (void *)rtud_head + rtud_head->data_off;
	vlan_tab_shm = wrs_shm_follow(rtud_head, rtu_hdr->vlans);
	if (!vlan_tab_shm)
		return -2;
	/* read data, with the sequential lock to have all data consistent */
	while (1) {
		ii = wrs_shm_seqbegin(rtud_head);
		memcpy(vlan_tab_local, vlan_tab_shm,
		       NUM_VLANS * sizeof(*vlan_tab_shm));
		retries++;
		if (retries > 100)
			return -1;
		if (!wrs_shm_seqretry(rtud_head, ii))
			break; /* consistent read */
		usleep(1000);
	}

	return 0;
}

/* Read filtes from rtud's shm, convert hashtable to regular table */
int shmem_rtu_read_htab(struct rtu_filtering_entry *rtu_htab_local, int *read_entries)
{
	unsigned ii;
	unsigned retries = 0;
	struct rtu_filtering_entry *htab_shm;
	struct rtu_shmem_header *rtu_hdr;
	struct rtu_filtering_entry *empty;

	rtu_hdr = (void *)rtud_head + rtud_head->data_off;
	htab_shm = wrs_shm_follow(rtud_head, rtu_hdr->filters);
	if (!htab_shm)
		return -2;

	/* Read data, with the sequential lock to have all data consistent */
	while (1) {
		ii = wrs_shm_seqbegin(rtud_head);
		memcpy(rtu_htab_local, htab_shm,
		       RTU_BUCKETS * HTAB_ENTRIES * sizeof(*htab_shm));
		retries++;
		if (retries > 100)
			return -1;
		if (!wrs_shm_seqretry(rtud_head, ii))
			break; /* consistent read */
		usleep(1000);
	}

	/* Convert hash table to ordered table. Table will be qsorted later,
	 * no need to qsort entire table */
	*read_entries = 0;
	empty = rtu_htab_local;
	for (ii = 0; ii < RTU_BUCKETS * HTAB_ENTRIES; ii++) {
		if (rtu_htab_local[ii].valid) {
			memcpy(empty, &rtu_htab_local[ii], sizeof(*htab_shm));
			empty++;
			(*read_entries)++;
		}
	}

	return 0;
}

int shmem_rtu_read_ports(struct rtu_port_entry *ports_tab_local, int *nports)
{
	unsigned ii;
	unsigned retries = 0;
	struct rtu_port_entry *rtu_ports_shm;
	struct rtu_shmem_header *rtu_hdr;

	rtu_hdr = (void *)rtud_head + rtud_head->data_off;
	rtu_ports_shm = wrs_shm_follow(rtud_head, rtu_hdr->rtu_ports);
	if (!rtu_ports_shm)
		return -2;

	/* read data, with the sequential lock to have all data consistent */
	while (1) {
		ii = wrs_shm_seqbegin(rtud_head);
		memcpy(ports_tab_local, rtu_ports_shm,
		       HAL_MAX_PORTS * sizeof(*rtu_ports_shm));
		*nports = rtu_hdr->rtu_nports;
		retries++;
		if (retries > 100) {
			pr_error("couldn't read consistent data from RTU's "
				 "shmem. Use inconsistent\n");
			break; /* use inconsistent data */
			}
		if (!wrs_shm_seqretry(rtud_head, ii))
			break; /* consistent read */
		usleep(1000);
	}
	return 0;
}

/* Compare entries by by MAC */
int cmp_rtu_entries_mac(const void *p1, const void *p2)
{
	const struct rtu_filtering_entry *e1 = p1;
	const struct rtu_filtering_entry *e2 = p2;

	return memcmp(e1->mac, e2->mac, 6);
}

/* Compare rtu entries by FID then by MAC */
int cmp_rtu_entries_fid_mac(const void *p1, const void *p2)
{
	const struct rtu_filtering_entry *e1 = p1;
	const struct rtu_filtering_entry *e2 = p2;

	if (e1->fid > e2->fid)
	    return 1;
	else if (e1->fid < e2->fid)
	    return -1;
	return memcmp(e1->mac, e2->mac, 6);
}

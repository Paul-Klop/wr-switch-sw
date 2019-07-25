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
int *ppsi_ppi_nlinks;

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

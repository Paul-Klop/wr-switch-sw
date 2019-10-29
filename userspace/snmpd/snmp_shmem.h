#ifndef WRS_SNMP_SHMEM_H
#define WRS_SNMP_SHMEM_H

#include <libwr/shmem.h>
#include <libwr/hal_shmem.h>
#include <ppsi/ppsi.h>
#include <libwr/hal_shmem.h>
#include <libwr/rtu_shmem.h>

/* HAL */
extern struct wrs_shm_head *hal_head;
extern struct hal_shmem_header *hal_shmem;
extern struct hal_port_state *hal_ports;
extern int hal_nports_local;

/* PPSI */
extern struct wrs_shm_head *ppsi_head;
extern struct pp_instance *ppsi_ppi;
extern parentDS_t *ppsi_parentDS;
extern int *ppsi_ppi_nlinks;

/* RTUd */
struct wrs_shm_head *rtud_head;

void init_shm();
int shmem_ready_hald(void);
int shmem_ready_ppsi(void);
int shmem_ready_rtud(void);

#endif /* WRS_SNMP_SHMEM_H */

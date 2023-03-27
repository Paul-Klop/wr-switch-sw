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
extern int *ppsi_ppi_nlinks;
extern parentDS_t *ppsi_parentDS;
extern defaultDS_t *ppsi_defaultDS;

/* RTUd */
extern struct wrs_shm_head *rtud_head;

void init_shm();
int shmem_ready_hald(void);
int shmem_ready_ppsi(void);
int shmem_ready_rtud(void);
int shmem_rtu_read_htab(struct rtu_filtering_entry *rtu_htab_local, int *read_entries);
int shmem_rtu_read_vlans(struct rtu_vlan_table_entry *vlan_tab_local);
int shmem_rtu_read_ports(struct rtu_port_entry *ports_tab_local, int *nports);

/* Compare entries by by MAC */
int cmp_rtu_entries_mac(const void *p1, const void *p2);
/* Compare rtu entries by FID then by MAC */
int cmp_rtu_entries_fid_mac(const void *p1, const void *p2);


#endif /* WRS_SNMP_SHMEM_H */

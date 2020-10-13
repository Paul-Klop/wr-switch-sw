#ifndef __LIBWR_RTU_SHMEM_H__
#define __LIBWR_RTU_SHMEM_H__

#include <stdint.h>

#define RTU_ENTRIES	2048
#define RTU_BUCKETS	4
#define HTAB_ENTRIES	((RTU_ENTRIES)/(RTU_BUCKETS))
#define LAST_HTAB_ENTRY	((HTAB_ENTRIES)-1)
#define LAST_RTU_BUCKET	(RTU_BUCKETS-1)

/* Maximum number of supported VLANs */
#define NUM_VLANS               4096

/* Currently only one port mirror setup is allowed */
#define NUM_MIRROR		1

#define ETH_ALEN 6
#define ETH_ALEN_STR 18

#define VALID_CONFIG 1<<31
#define VALID_QMODE  1<<0
#define VALID_PRIO   1<<1
#define VALID_VID    1<<2
#define VALID_FID    1<<3
#define VALID_UNTAG  1<<4
#define VALID_PMASK  1<<5
#define VALID_DROP   1<<6

#define QMODE_ACCESS   0
#define QMODE_TRUNK    1
#define QMODE_DISABLED 2
#define QMODE_UNQ      3
#define QMODE_INVALID  4


#define RTU_VID_MIN 0
#define RTU_VID_MAX 4094

#define RTU_FID_MIN 0
#define RTU_FID_MAX 4094

#define RTU_PRIO_MIN 0
#define RTU_PRIO_MAX 7
#define RTU_PRIO_DISABLE -1

#define PORT_PRIO_MIN		RTU_PRIO_MIN
#define PORT_PRIO_MAX		RTU_PRIO_MAX
#define PORT_PRIO_DISABLE	RTU_PRIO_DISABLE

#define PORT_VID_MIN RTU_VID_MIN
#define PORT_VID_MAX RTU_VID_MAX

#define RTU_PMASK_MIN 0
#define RTU_PMASK_MAX(n_ports) ((1 << n_ports) - 1)

/* RTU entry address */
struct rtu_addr {
	int hash;
	int bucket;
};

/* Filtering entries may be static (permanent) or dynamic (learned) */
#define RTU_ENTRY_TYPE_DYNAMIC 1
#define RTU_ENTRY_TYPE_STATIC 0

/* When adding MAC entry, the Default VID is 0x0 */
#define RTU_ENTRY_VID_DEFAULT 0

/* helper to verify correctness of a rtu type */
static inline int rtu_check_type(int type)
{
	switch (type) {
	case RTU_ENTRY_TYPE_DYNAMIC:
	case RTU_ENTRY_TYPE_STATIC:
		/* type ok */
		return 0;
	default:
		return -1;
	}
}

static inline char *rtu_type_to_str(int type)
{
	switch (type) {
	case RTU_ENTRY_TYPE_DYNAMIC:
		return "DYNAMIC";
	case RTU_ENTRY_TYPE_STATIC:
		return "STATIC";
	default:
		return "Unknown";
	}
}


/**
 * \brief RTU Filtering Database Entry Object
 */
struct rtu_filtering_entry {
	struct rtu_addr addr;	/* address of self in the RTU hashtable */

	int valid;		/* bit: 1 = entry is valid, 0: entry is
				 * invalid (empty) */
	int end_of_bucket;	/* bit: 1 = last entry in current bucket,
				 * stop search at this point */
	int is_bpdu;		/* bit: 1 = BPDU (or other non-STP-dependent
				 * packet) */

	uint8_t mac[ETH_ALEN];	/* MAC address (for searching the  bucketed
				 * hashtable) */
	uint8_t fid;		/* Filtering database ID (for searching the
				 * bucketed hashtable) */

	uint32_t port_mask_src; /* port mask for source MAC addresses. Bits
				 * set to 1 indicate that packet having this
				 * MAC address can be forwarded from these
				 * corresponding ports. Ports having their
				 * bits set to 0 shall drop the packet. */

	uint32_t port_mask_dst;	/* port mask for destination MAC address. Bits
				 * set to 1 indicate to which physical ports
				 * the packet with matching destination MAC
				 * address shall be routed */

	int drop_when_source;	/* bit: 1 = drop the packet when source
				 * address matches */
	int drop_when_dest;	/* bit: 1 = drop the packet when destination
				 * address matches */
	int drop_unmatched_src_ports;	/* bit: 1 = drop the packet when it
					 * comes from source port different
					 * than specified in port_mask_src */

	uint32_t last_access_t;	/* time of last access to the rule
				 * (for aging) */

	int force_remove;	/* when true, the entry is to be removed
				 * immediately (aged out or destination port
				 * went down) */

	uint8_t prio_src;	/* priority (src MAC) */
	int has_prio_src;	/* priority value valid */
	int prio_override_src;	/* priority override (force per-MAC priority) */

	uint8_t prio_dst;	/* priority (dst MAC) */
	int has_prio_dst;	/* priority value valid */
	int prio_override_dst;	/* priority override (force per-MAC priority) */

	int dynamic;
	int age;
};

/**
 * \brief RTU VLAN registration entry object
 */
struct rtu_vlan_table_entry {
	uint32_t port_mask;	/* VLAN port mask:
				 * 1 = ports assigned to this VLAN */
	uint8_t fid;		/* Filtering Database Identifier */
	uint8_t prio;		/* VLAN priority */
	int has_prio;		/* priority defined; */
	int prio_override;	/* priority override
				 * (force per-VLAN priority) */
	int drop;		/* 1: drop the packet (VLAN not registered) */
};

/**
 * \brief RTU mirroring configuration
 */
struct rtu_mirror_info {
	int en;			/* Mirroring enabled flag */
	uint32_t imask;		/* Ingress source port mask */
	uint32_t emask;		/* Egress source port mask */
	uint32_t dmask;		/* Destination port mask */
};

/**
 * \brief RTU port configuration
 */
struct rtu_port_entry {
	uint8_t qmode;		/* q mode of a port */
	uint8_t fix_prio;	/* is fix priority set */
	uint8_t prio;		/* VLAN priority */
	uint8_t untag;          /* untag */ 
	uint16_t pvid;		/* PVID  */
	uint8_t mac[ETH_ALEN];	/* MAC of a port */
};

/* This is the overall structure stored in shared memory */
#define RTU_SHMEM_VERSION 6 /* Version 6, remove *_offset */
struct rtu_shmem_header {
	struct rtu_filtering_entry *filters;
	struct rtu_vlan_table_entry *vlans;
	struct rtu_mirror_info *mirror;
	struct rtu_port_entry *rtu_ports;
	uint32_t rtu_nports;
};

#endif /*  __LIBWR_RTU_SHMEM_H__ */

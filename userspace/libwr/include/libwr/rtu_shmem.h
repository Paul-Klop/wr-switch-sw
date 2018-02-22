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

#define ETH_ALEN 6
#define ETH_ALEN_STR 18

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

/* This is the overall structure stored in shared memory */
#define RTU_SHMEM_VERSION 2 /* Version 2, added filters_offset and
			     * vlans_offset */
struct rtu_shmem_header {
	struct rtu_filtering_entry *filters;
	struct rtu_vlan_table_entry *vlans;
	unsigned long filters_offset;
	unsigned long vlans_offset;
};

#endif /*  __LIBWR_RTU_SHMEM_H__ */

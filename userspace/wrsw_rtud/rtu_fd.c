/*\
 * White Rabbit RTU (Routing Table Unit)
 * Copyright (C) 2010, CERN.
 *
 * Version:     wrsw_rtud v1.0
 *
 * Authors:     Juan Luis Manas (juan.manas@integrasys.es)
 *              Miguel Baizan   (miguel.baizan@integrasys.es)
 *              Maciej Lipinski (maciej.lipinski@cern.ch)
 *
 * Description: RTU Filtering database.
 *              Filtering database management related operations and filtering
 *              database mirror. Note there is a single Filtering Database
 *              object per Bridge (See 802.1Q - 12.7.1)
 *
 * Fixes:
 *              Alessandro Rubini
 *              Tomasz Wlostowski
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include <fpga_io.h>
#include <regs/rtu-regs.h>

#include <libwr/wrs-msg.h>
#include <libwr/shmem.h>
#include <libwr/rtu_shmem.h>
#include <libwr/util.h>

#include "rtu_fd.h"
#include "rtu_drv.h"
#include "rtu_hash.h"

// Used to declare HW request types.
#define HW_WRITE_REQ     0
#define HW_REMOVE_REQ    1

/**
 * \brief Filtering Database entry handle.
 */
struct fd_handle {
	struct rtu_addr addr;
	struct rtu_filtering_entry *entry_ptr; /* pointer to entry at mirror
						* fd */
};

/**
 * \brief HW (HTAB or HCAM) write request.
 */
struct hw_req {
	int type;		// WRITE or CLEAN
	struct fd_handle handle;	// filtering database entry handle
	struct hw_req *next;	// linked list
};

/**
 * \brief HW write and clean requests list.
 * Used to temporarily store entry changes performed at SW.
 */
struct hw_req *hw_req_list;

/**
 * \brief Mirror of ZBT SRAM memory MAC address table.
 * Main filtering table organized as hash table with 4-entry buckets.
 * Note both banks have the same content. Therefore SW only mirrors one bank.
 */
static struct rtu_filtering_entry (*rtu_htab)[RTU_BUCKETS];

/**
 * \brief Max time that a dynamic MAC entry can remain
 * in the MAC table after being used. [seconds]
 */
static unsigned long aging_time = DEFAULT_AGING_TIME;

/**
 * Mirror of VLAN table
 */
static struct rtu_vlan_table_entry *vlan_tab;

/**
 * Mirror of port mirroring configuration
 */
static struct rtu_mirror_info *mirror_cfg;

/**
 * \brief Mutex used to synchronise concurrent access to the filtering database.
 */
static pthread_mutex_t fd_mutex;

/**
 * \brief Pointer to shmem, user for write locking.
 */
static struct wrs_shm_head *rtu_port_shmem;

static struct hw_req *tail(struct hw_req *head);
static void clean_list(struct hw_req *head);
static int hw_request(int type, struct rtu_addr addr,
		      struct rtu_filtering_entry *ent);

static void clean_fd(void);
static void clean_vd(void);
static void clean_mc(void);

static void rtu_hw_commit(void);
static void rtu_fd_commit(void);

static void rtu_fd_age_out(void);
static void rtu_fd_age_update(void);

/**
 * \brief Initializes the RTU filtering database.
 * @param poly hash polinomial.
 * @param aging aging time
 */
int rtu_fd_init(uint16_t poly, unsigned long aging)
{
	uint32_t bitmap[RTU_ENTRIES / 32];
	int err;
	struct rtu_shmem_header *rtu_hdr;
	pr_debug("Open rtu shmem.\n");
	rtu_port_shmem = wrs_shm_get(wrs_shm_rtu, "wrsw_rtud",
				WRS_SHM_WRITE | WRS_SHM_LOCKED);
	if (!rtu_port_shmem) {
		pr_error("%s: Can't join shmem: %s\n", __func__,
			 strerror(errno));
		return -1;
	}

	if (rtu_port_shmem->pidsequence == 1) {
		/* for first RTUd run */
		pr_debug("Alloc rtu_hdr\n");
		rtu_hdr = wrs_shm_alloc(rtu_port_shmem, sizeof(*rtu_hdr));
	} else {
		/* rtu_hdr was created at header->offset */
		rtu_hdr = (void *)rtu_port_shmem + rtu_port_shmem->data_off;
		/* move data_size to have have similar behavior like
		 * wrs_shm_alloc, needed for future allocations */
		/* force 8-alignment */
		rtu_port_shmem->data_size += (sizeof(*rtu_hdr) + 7) & ~7;
	}
	if (!rtu_hdr) {
		pr_error("%s: Cannot allocate mem in shmem rtu_hdr\n",
			 __func__);
		return -1;
	}

	if (!rtu_hdr->filters) {
		/* for first RTUd run */
		pr_info("Allocating a new, clean hash table\n");
		rtu_htab = wrs_shm_alloc(rtu_port_shmem,
					sizeof(*rtu_htab) * HTAB_ENTRIES);
		rtu_hdr->filters = (struct rtu_filtering_entry *) rtu_htab;
		rtu_hdr->filters_offset =
				(void *)rtu_htab - (void *)rtu_port_shmem;
		pr_debug("Clean filtering database.\n");
		clean_fd();		/* clean filtering database */
	} else {
		pr_info("Use existing filtering table.\n");
		/* next RTUd runs */
		rtu_hdr->filters =
			      (void *)rtu_port_shmem + rtu_hdr->filters_offset;
		rtu_htab = (void *)rtu_hdr->filters;
		/* move data_size to have have similar behavior like
		 * wrs_shm_alloc, needed for future allocations */
		/* force 8-alignment */
		rtu_port_shmem->data_size +=
				(sizeof(*rtu_htab) * HTAB_ENTRIES + 7) & ~7;
	}

	if (!rtu_hdr->vlans) {
		/* for first RTUd run */
		pr_info("Allocating a new, clean vlan table\n");
		vlan_tab = wrs_shm_alloc(rtu_port_shmem,
					sizeof(*vlan_tab) * NUM_VLANS);
		rtu_hdr->vlans = vlan_tab;
		rtu_hdr->vlans_offset =
				(void *)vlan_tab - (void *)rtu_port_shmem;
		pr_debug("Clean vlan database.\n");
		clean_vd();		/* clean VLAN database */
	} else {
		pr_info("Using existing vlan table.\n");
		/* next RTUd runs */
		rtu_hdr->vlans =
				(void *)rtu_port_shmem + rtu_hdr->vlans_offset;
		vlan_tab = (void *)rtu_hdr->vlans;
		/* move data_size to have have similar behavior like
		 * wrs_shm_alloc, needed for future allocations */
		/* force 8-alignment */
		rtu_port_shmem->data_size +=
				(sizeof(*vlan_tab) * NUM_VLANS + 7) & ~7;
	}

	if (!rtu_hdr->mirror) {
		/* for first RTUd run */
		pr_info("Allocating a new, port mirroring config\n");
		mirror_cfg = wrs_shm_alloc(rtu_port_shmem,
					sizeof(*mirror_cfg) * NUM_MIRROR);
		rtu_hdr->mirror = mirror_cfg;
		rtu_hdr->mirror_offset =
				(void *)mirror_cfg - (void *)rtu_port_shmem;
		pr_debug("Clean vlan database.\n");
		clean_mc();		/* clean port mirroring config */
	} else {
		pr_info("Using existing port mirroring config.\n");
		/* next RTUd runs */
		rtu_hdr->mirror =
				(void *)rtu_port_shmem + rtu_hdr->mirror_offset;
		mirror_cfg = (void *)rtu_hdr->mirror;
		/* move data_size to have have similar behavior like
		 * wrs_shm_alloc, needed for future allocations
		 * force 8-alignment
		 */
		rtu_port_shmem->data_size +=
				(sizeof(*mirror_cfg) * NUM_MIRROR + 7) & ~7;
	}

	if ((!rtu_htab) || (!vlan_tab) || (!mirror_cfg)) {
		pr_error("%s: Cannot allocate mem in shmem\n", __func__);
		return -1;
	}
	/* add version info */
	rtu_port_shmem->version = RTU_SHMEM_VERSION;

	pr_debug("clean aging map.\n");
	rtu_read_aging_bitmap(bitmap);	// clean aging registers
	pr_debug("set aging time [%ld].\n", aging);
	aging_time = aging;

	err = pthread_mutex_init(&fd_mutex, NULL);
	if (err)
		return err;

	pr_debug("set hash poly.\n");
	rtu_fd_set_hash_poly(poly);

	/* release process waiting on rtud's shm
	 NOTE: all data may not be populated yet */
	wrs_shm_write(rtu_port_shmem, WRS_SHM_WRITE_END);

	return 0;
}

static int htab_search(uint8_t mac[ETH_ALEN],
		       uint8_t fid, struct rtu_filtering_entry **ent)
{
	int i, j;

	for (i = 0; i < HTAB_ENTRIES; i++)
		for (j = 0; j < RTU_BUCKETS; j++) {
			struct rtu_filtering_entry *tmp = &rtu_htab[i][j];

			if (!tmp->valid)
				continue;

			if (mac_equal(tmp->mac, mac) && tmp->fid == fid) {
				*ent = tmp;
				return 1;
			}
		}

	return 0;
}

static int htab_count_buckets(struct rtu_addr addr)
{
	int n = 0, i;

	for (i = 0; i < RTU_BUCKETS; i++)
		if (rtu_htab[addr.hash][i].valid)
			n++;

	return n;
}

/**
 * \brief Creates or updates a filtering entry in the filtering database.
 * @param mac MAC address specification
 * @param vid VLAN identifier
 * @param port_map a port map specification with a control element for each
 * outbound port to specify filtering for that MAC address specification and VID
 * @param dynamic it indicates whether it's a dynamic entry
 * @return 0 if entry was created or updated. -ENOMEM if no space is available.
 */
int rtu_fd_create_entry(uint8_t mac[ETH_ALEN], uint16_t vid, uint32_t port_mask,
			int dynamic, int at_existing_entry)
{
	struct rtu_filtering_entry *ent; /* pointer to scan hashtable */
	uint8_t fid;		// Filtering database identifier
	int ret = 0;		// return value
	uint32_t mask_src, mask_dst;	// used to check port masks update
	struct rtu_addr eaddr;

	pthread_mutex_lock(&fd_mutex);
	wrs_shm_write(rtu_port_shmem, WRS_SHM_WRITE_BEGIN);

	// if VLAN is registered (otherwise just ignore request)
	if (!vlan_tab[vid].drop) {
		// Obtain FID from VLAN database
		fid = vlan_tab[vid].fid;

		/* Case 1: entry already present in the hashtable */
		if (htab_search(mac, fid, &ent)) {
			pr_debug("Entry for mac %s already found.\n",
				  mac_to_string(mac));

			if (at_existing_entry == ADD_TO_EXISTING)	// enable multipath for redundancy
				mask_dst = port_mask | ent->port_mask_dst;
			else
				mask_dst = port_mask;	//ML: aging bugfix-> if we receive ureq for an existing entry,
			//it means that the port moved, so we override the existing mask...
			mask_src = 0xFFFFFFFF;	//ML: filtering on ingress is optional according to 802.1Q-2012
			//by default it should not happen. TODO: add optional config
			if ((ent->port_mask_dst != mask_dst)
			    || (ent->port_mask_src != mask_src)
			    || (ent->dynamic != dynamic)) {
				/* new entry */
				ent->port_mask_dst = mask_dst;
				ent->port_mask_src = mask_src;
				ent->dynamic = dynamic;
				/* ML: update time always when updating
				 * the entry */
				ent->last_access_t = get_monotonic_sec();
				hw_request(HW_WRITE_REQ, ent->addr, ent);
			}
			/* Case 2: MAC not found */
		} else {
			int n_buckets;

			eaddr.hash = rtu_hash(mac, fid);
			n_buckets = htab_count_buckets(eaddr);

			if (n_buckets == RTU_BUCKETS) {
				pr_error("Hash %03x has no buckets left.\n",
				      eaddr.hash);
				wrs_shm_write(rtu_port_shmem,
					      WRS_SHM_WRITE_END);
				pthread_mutex_unlock(&fd_mutex);
				return -ENOMEM;
			}

			eaddr.bucket = n_buckets;

			ent = &rtu_htab[eaddr.hash][eaddr.bucket];
			ent->addr = eaddr;

			pr_debug(
			      "Created new entry for MAC %s : hash %03x:%d.\n",
			      mac_to_string(mac), eaddr.hash, eaddr.bucket);

			ent->valid = 1;
			ent->fid = fid;
			ent->port_mask_dst = port_mask;
			ent->port_mask_src = 0xFFFFFFFF;	//ML: filtering on ingress is optional according to 802.1Q-2012
			//by default it should not happen. TODO: add optional config
			ent->dynamic = dynamic;
			ent->last_access_t = get_monotonic_sec();
			mac_copy(ent->mac, mac);
			hw_request(HW_WRITE_REQ, eaddr, ent);
		}

	}
	rtu_fd_commit();
	wrs_shm_write(rtu_port_shmem, WRS_SHM_WRITE_END);
	pthread_mutex_unlock(&fd_mutex);
	return ret;
}

/**
 * \brief Set the polynomial used for hash calculation.
 * Changing the hash polynomial requires removing any existing
 * entry from RTU table.
 * Note in case RTU table becomes full, this function may
 * be used to change hash polynomial (thus leading to a different hash
 * distribution).
 * @param poly binary polynomial representation.
 * CRC-16-CCITT -> 1+x^5+x^12+x^16          -> 0x1021
 * CRC-16-IBM   -> 1+x^2+x^15+x^16          -> 0x8005
 * CRC-16-DECT  -> 1+x^3+x^7+x^8+x^10+x^16  -> 0x0589
 */
void rtu_fd_set_hash_poly(uint16_t poly)
{
	pthread_mutex_lock(&fd_mutex);
	rtu_write_hash_poly(poly);
	rtu_hash_set_poly(poly);
	pthread_mutex_unlock(&fd_mutex);
}

/**
 * \brief Sets the aging time for dynamic filtering entries.
 * @param t new aging time value [seconds].
 * @return -EINVAL if t < 10 or t > 1000000 (802.1Q, Table 8.3); 0 otherwise.
 */
int rtu_fd_set_aging_time(unsigned long t)
{
	if ((t < 10) || (t > 1000000))
		return -EINVAL;
	aging_time = t;
	return 0;
}

/**
 * \brief Deletes old filtering entries from filtering database to support
 * changes in active topology.
 */
void rtu_fd_flush(void)
{
	rtu_fd_age_update();	// Update filtering entries age

	pthread_mutex_lock(&fd_mutex);
	rtu_fd_age_out();	// Remove old entries
	pthread_mutex_unlock(&fd_mutex);
}

//---------------------------------------------
// Static Methods
//---------------------------------------------

/**
 * Returns pointer to last element in hw_req_list.
 */
static struct hw_req *tail(struct hw_req *head)
{
	struct hw_req *ptr;

	for (ptr = head; ptr->next; ptr = ptr->next) ;
	return ptr;
}

/**
 * Removes all elements from the hw_req_list
 */
static void clean_list(struct hw_req *head)
{
	struct hw_req *ptr;

	while (head) {
		ptr = head;
		head = head->next;
		free(ptr);
	}
}

static int hw_request(int type, struct rtu_addr addr,
		      struct rtu_filtering_entry *ent)
{
	struct hw_req *req;

	req = (struct hw_req *)malloc(sizeof(struct hw_req));
	if (!req)
		return -ENOMEM;

	req->type = type;
	req->handle.addr = addr;
	req->handle.entry_ptr = ent;
	req->next = NULL;

	if (!hw_req_list)
		hw_req_list = req;
	else
		tail(hw_req_list)->next = req;

	return 0;
}

static inline int to_mem_addr(struct rtu_addr addr)
{
	return ((0x07FF & addr.hash) << 5) | ((0x0003 & addr.bucket) << 3);
}

/**
 * \brief Checks whether a given pair (mac,fid) is at HTAB
 * @param mac mac address
 * @param fid filtering database identifier
 * @param bucket inout param.Returns the bucket number where the entry was found
 * @param ent pointer to entry found.
 * @return 0 if entry was not found. 1 if entry was found. -1 if not found and
 * HTAB was full for the corresponding hash. -EINVAL if bucket >= RTU_BUCKETS
 */

/**
 * Filtering database initialisation.
 */
static void clean_fd(void)
{
	wrs_shm_write(rtu_port_shmem, WRS_SHM_WRITE_BEGIN);

	memset(rtu_htab, 0, sizeof(*rtu_htab) * HTAB_ENTRIES);

	rtu_clean_htab();
	wrs_shm_write(rtu_port_shmem, WRS_SHM_WRITE_END);
}

/**
 * VLAN database initialisation. VLANs are initially marked as disabled.
 */
static void clean_vd(void)
{
	int i;

	wrs_shm_write(rtu_port_shmem, WRS_SHM_WRITE_BEGIN);

	rtu_clean_vlan();
	for (i = 1; i < NUM_VLANS; i++) {
		vlan_tab[i].drop = 1;
		vlan_tab[i].port_mask = 0x0;
	}

	// First entry reserved for untagged packets.
	vlan_tab[0].port_mask = 0xffffffff;
	vlan_tab[0].drop = 0;
	vlan_tab[0].fid = 0;
	vlan_tab[0].has_prio = 0;
	vlan_tab[0].prio_override = 0;
	vlan_tab[0].prio = 0;

	rtu_write_vlan_entry(0, &vlan_tab[0]);
	wrs_shm_write(rtu_port_shmem, WRS_SHM_WRITE_END);
}

/**
 * Port mirroring config initialization (disabled by default).
 */
static void clean_mc(void)
{
	wrs_shm_write(rtu_port_shmem, WRS_SHM_WRITE_BEGIN);

	mirror_cfg[0].en = 0;
	mirror_cfg[0].imask = 0x0;
	mirror_cfg[0].emask = 0x0;
	mirror_cfg[0].dmask = 0x0;

	rtu_enable_mirroring(mirror_cfg[0].en);
	rtu_cfg_mirroring(mirror_cfg);
	wrs_shm_write(rtu_port_shmem, WRS_SHM_WRITE_END);
}

/**
 * \brief Updates the age of filtering entries accessed in the last period.
 */
static void rtu_fd_age_update(void)
{
	int i;			// Aging Bitmap word loop index
	int j;			// Word bits loop index
	uint32_t agr_word;	// Aux var for manipulating aging RAM
	uint16_t hash;		// HTAB entry hash (index)
	int bucket;		// HTAB entry bucket
	int bit_cnt;		// Absolute bit counter
	unsigned long t;	// Time since epoch (secs)

	uint32_t bitmap[RTU_ENTRIES / 32];

	rtu_read_aging_bitmap(bitmap);

	// Update 'last access time' for accessed entries
	t = get_monotonic_sec();
	// HTAB
	wrs_shm_write(rtu_port_shmem, WRS_SHM_WRITE_BEGIN);
	for (i = 0; i < RTU_ENTRIES / 32; i++)
		for (j = 0; j < 32; j++) {
			agr_word = bitmap[i];
			if (agr_word & (1 << j)) {
				bit_cnt = (i << 5) | j;
				hash = bit_cnt >> 2;	// 4 buckets per hash
				bucket = bit_cnt & 0x03;	// last 2 bits

				if (rtu_htab[hash][bucket].dynamic
				    == RTU_ENTRY_TYPE_STATIC)
					continue;

				if (0)
					pr_debug(
					      "Updated htab entry age: mac = %s, hash = %03x:%d, delta_t = %ld\n",
					      mac_to_string(rtu_htab[hash]
							    [bucket].mac), hash,
					      bucket,
					      t -
					      rtu_htab[hash][bucket].
					      last_access_t);

				rtu_htab[hash][bucket].age =
				    t - rtu_htab[hash][bucket].last_access_t;
				rtu_htab[hash][bucket].last_access_t = t;
			}
		}
	wrs_shm_write(rtu_port_shmem, WRS_SHM_WRITE_END);

}

static int is_unicast(uint32_t port_mask)
{
	return port_mask && !(port_mask & (port_mask - 1));
}

int rtu_fd_remove_entry(uint8_t *mac, uint32_t port_mask, int type)
{
	int i;	/* loop index */
	int j;	/* bucket loop index */
	struct rtu_filtering_entry *ent; /* pointer to scan tables */
	int removed_entries = 0;

	if (!port_mask)
		pr_error("Empty port mask for MAC %s\n", mac_to_string(mac));

	wrs_shm_write(rtu_port_shmem, WRS_SHM_WRITE_BEGIN);
	pr_debug("Looking for an entry with mask=0x%x MAC: %s type %s\n",
		 port_mask, mac_to_string(mac),
		 rtu_type_to_str(type));
	for (i = HTAB_ENTRIES; i-- > 0;) {
		for (j = RTU_BUCKETS; j-- > 0;) {
			ent = &rtu_htab[i][j];
			if (ent->valid
			    && (ent->dynamic == type)
			    && !memcmp(ent->mac, mac, ETH_ALEN)
			    && (ent->port_mask_dst == port_mask)) {
				/* entry is _only_ for this port */
				hw_request(HW_REMOVE_REQ, ent->addr,
					    ent);
				pr_debug("Cleaning %s entry for mask=0x%x MAC: "
					"%s type %s\n",
					is_unicast(port_mask) ?
						      "unicast" : "multicast",
					ent->port_mask_dst,
					mac_to_string(ent->mac),
					rtu_type_to_str(type));
				removed_entries++;
			}
		}
	}
	/* commit changes */
	rtu_fd_commit();
	wrs_shm_write(rtu_port_shmem, WRS_SHM_WRITE_END);
	return removed_entries;
}

void rtu_fd_clear_entries_for_port(int dest_port, int type)
{
	int i;			// loop index
	int j;			// bucket loop index
	struct rtu_filtering_entry *ent; /* pointer to scan tables */

	wrs_shm_write(rtu_port_shmem, WRS_SHM_WRITE_BEGIN);

	for (i = HTAB_ENTRIES; i-- > 0;) {
		for (j = RTU_BUCKETS; j-- > 0;) {
			ent = &rtu_htab[i][j];
			if (ent->valid
			    && (ent->dynamic == type)) {
				if (ent->port_mask_dst == (1 << dest_port)) {
					/* entry is _only_ for this port */
					hw_request(HW_REMOVE_REQ, ent->addr,
						   ent);
				}
				else if (ent->port_mask_dst & (1 << dest_port)) {
					/* entry is for at least two ports 
					 * including current */
					pr_error("cleaning multicast entries "
						 "not supported yet... "
						 "mask=0x%x port=%d (wri%d)\n",
						 ent->port_mask_dst,
						 dest_port + 1, dest_port + 1);
				}
			}
		}
	}
	// commit changes
	rtu_fd_commit();
	wrs_shm_write(rtu_port_shmem, WRS_SHM_WRITE_END);
}

/**
 * For each filtering entry in the filtering database, this method checks its
 * last access time and removes it in case entry is older than the aging time.
 */
static void rtu_fd_age_out(void)
{
	int i;			// loop index
	int j;			// bucket loop index
	struct rtu_filtering_entry *ent; /* pointer to scan tables */
	unsigned long t;	// (secs)

	t = get_monotonic_sec() - aging_time;
	wrs_shm_write(rtu_port_shmem, WRS_SHM_WRITE_BEGIN);

	// HTAB
	for (i = HTAB_ENTRIES; i-- > 0;) {
		for (j = RTU_BUCKETS; j-- > 0;) {
			ent = &rtu_htab[i][j];
			if (ent->valid && ent->dynamic == RTU_ENTRY_TYPE_DYNAMIC
			    && (time_after(t, ent->last_access_t)
				|| ent->force_remove)) {
				pr_debug("Deleting htab entry: mac = %s, hash ="
				      " 0x%x, bucket = %d, forced=%d\n",
				      mac_to_string(ent->mac), i, j,
				      ent->force_remove);
				hw_request(HW_REMOVE_REQ, ent->addr, ent);
			}
		}
	}
	// commit changes
	rtu_fd_commit();
	wrs_shm_write(rtu_port_shmem, WRS_SHM_WRITE_END);
}

/**
 * \brief Read changes from hw_req_list and invoke RTU driver to efectively
 * write or clean the entry.
 */
static void delete_htab_entry(struct rtu_addr addr)
{
	int i, n_buckets = htab_count_buckets(addr);

	pr_debug("Deleted entry for MAC %s : hash %03x:%d.\n",
	      mac_to_string(rtu_htab[addr.hash][addr.bucket].mac), addr.hash,
	      addr.bucket);

	memset(&rtu_htab[addr.hash][addr.bucket], 0,
	       sizeof(struct rtu_filtering_entry));

	if (addr.bucket < n_buckets - 1)
		memmove(&rtu_htab[addr.hash][addr.bucket],
			&rtu_htab[addr.hash][addr.bucket + 1],
			(n_buckets - addr.bucket -
			 1) * sizeof(struct rtu_filtering_entry));

	for (i = 0; i < n_buckets; i++) {
		struct rtu_addr a;
		a.hash = addr.hash;
		a.bucket = i;
		rtu_write_htab_entry(to_mem_addr(a),
				     &rtu_htab[a.hash][a.bucket],
				     (i == n_buckets - 1) ? 1 : 0);
	}
}

static void rtu_hw_commit(void)
{
	struct hw_req *req;	// used to scan hw_req_list

	for (req = hw_req_list; req; req = req->next) {
		switch (req->type) {
		case HW_WRITE_REQ:
			rtu_write_htab_entry(to_mem_addr(req->handle.addr),
					     req->handle.entry_ptr, 1);
			break;

		case HW_REMOVE_REQ:
			delete_htab_entry(req->handle.addr);
			break;

		}
	}
}

/**
 * \brief Commits entry changes at software to hardware HTAB and HCAM.
 */
static void rtu_fd_commit(void)
{
	if (!hw_req_list)
		return;

	rtu_hw_commit();

	// this list no longer needed
	clean_list(hw_req_list);
	hw_req_list = NULL;
}

/**
 * \brief Creates or updates a filtering entry in the VLAN table.
 * @param vid       VLAN ID
 * @param port_mask mask of ports on which VLAN is registered
 * @param fid       Filtering Database ID
 * @param prio      Priority to override the VLAN-tagged receive frames(if prio_override true)
 * @param has_prio  Indicates there is valid PRIO
 * @param prio_override Use the PRIO in the VLAN Table to override the frame's PRIO in the tag 
 * @param drop      Drop frames with this VID
 */
//--------------------------------------------------------------------------------------------
void rtu_fd_create_vlan_entry(int vid, uint32_t port_mask, uint8_t fid,
			      uint8_t prio, int has_prio, int prio_override,
			      int drop)
{
    /*****************************************************************************************
     * this is obviously wrong - the number of ports needs to be passed here in some beautiful
     * way using beautiful code. We need the number of ports to know where is CPU (always
     * the n-th port) -> this is to add CPU to the VLAN so taht all the protocols can reach
     * CPU. The alternative to that is set the HW forwarding which is less felxible. So, in
     * principle we might not need it in the end, but it would be good if it was possible to 
     * say whether we want or not
     */
#include <stddef.h>
#define rtu_rd(reg) \
	 _fpga_readl(FPGA_BASE_RTU + offsetof(struct RTU_WB, reg))
	int port_num = RTU_PSR_N_PORTS_R(rtu_rd(PSR));
	wrs_shm_write(rtu_port_shmem, WRS_SHM_WRITE_BEGIN);
    /****************************************************************************************/
	if (port_mask == 0x0 && drop == 1)
		vlan_tab[vid].port_mask = 0x0;
	else
		vlan_tab[vid].port_mask = port_mask | (0x1 << port_num);
	vlan_tab[vid].drop = drop;
	vlan_tab[vid].fid = fid;
	vlan_tab[vid].has_prio = has_prio;
	vlan_tab[vid].prio_override = prio_override;
	vlan_tab[vid].prio = prio;

	rtu_write_vlan_entry(vid, &vlan_tab[vid]);
	wrs_shm_write(rtu_port_shmem, WRS_SHM_WRITE_END);
}

void rtu_fd_write_mirror_config(int en, uint32_t imask, uint32_t emask,
		uint32_t dmask)
{
	wrs_shm_write(rtu_port_shmem, WRS_SHM_WRITE_BEGIN);

	mirror_cfg[0].en = en;
	/* update masks in SHM only if non-zero, or if disabling mirroring */
	if (en == 0 || imask != 0)
		mirror_cfg[0].imask = imask;
	if (en == 0 || emask != 0)
		mirror_cfg[0].emask = emask;
	if (en == 0 || dmask != 0)
		mirror_cfg[0].dmask = dmask;

	rtu_enable_mirroring(0);
	rtu_cfg_mirroring(mirror_cfg);
	rtu_enable_mirroring(en);
	wrs_shm_write(rtu_port_shmem, WRS_SHM_WRITE_END);
}

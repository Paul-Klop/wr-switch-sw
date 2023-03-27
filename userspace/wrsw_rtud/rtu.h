/*
 * White Rabbit RTU (Routing Table Unit)
 * Copyright (C) 2010, CERN.
 *
 * Version:     wrsw_rtud v1.0
 *
 * Authors:     Juan Luis Manas (juan.manas@integrasys.es)
 *              Maciej Lipinski (maciej.lipinski@cern.ch)
 *
 * Description: RTU data structures definition.
 *
 * Fixes:
 *              Tomasz Wlostowski
 *
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

#ifndef __WHITERABBIT_RTU_H
#define __WHITERABBIT_RTU_H

#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <libwr/rtu_shmem.h>


#define ENTRY_WORDS             8

#define MIN_PORT 0
#define MAX_PORT 17

#define NUM_RESERVED_ADDR       16

// Default aging time for dynamic entries at filtering database [secs]
#define DEFAULT_AGING_TIME      300
#define MIN_AGING_TIME          10
#define MAX_AGING_TIME          10000
// Default aging time resolution [secs]
#define DEFAULT_AGING_RES       20

// Used for clean_*() functions
#define SHM_NOT_LOCK 0
#define SHM_LOCK     1

// Keeping in mind year 2038
#define time_after(a,b) ((long)(b) - (long)(a) < 0)

extern struct wrs_shm_head *hal_head;
extern struct hal_port_state *hal_ports;
extern int hal_nports_local;

/**
 * \brief Pointer to shmem, used for write locking.
 */
extern struct wrs_shm_head *rtu_shmem_p;
/**
 * Mirror of port configuration
 */
extern struct rtu_port_entry *ports_cfg;

/**
 * \brief RTU request: input for the RTU
 */

struct rtu_request {
	int port_id;		// physical port identifier
	uint8_t src[ETH_ALEN];	// source MAC address
	uint8_t dst[ETH_ALEN];	// destination MAC address
	uint16_t vid;		// VLAN ID from the packet header
	int has_vid;		// non-zero: VID is present,0:untagged packet (VID=0)
	uint8_t prio;		// packet priority (either assigned by the port
	// or extracted from packet header)
	int has_prio;		// non-zero: priority present, 0:no priority defined
};


/**
 * \brief Copies src filtering entry body into dst filtering entry body.
 * @return pointer to dst filtering entry.
 */
static inline struct rtu_filtering_entry *rtu_fe_copy(
					struct rtu_filtering_entry *dst,
					struct rtu_filtering_entry *src)
{
	return memcpy(dst, src, sizeof(*src));
}

/**
 * \brief Cleans MAC entry from mirror MAC table. All entry fields are reset.
 * @param ent pointer to entry to clean (either in HCAM or HTAB)
 * @return pointer to filtering entry that was cleaned
 */
static inline struct rtu_filtering_entry *rtu_fe_clean(
					struct rtu_filtering_entry *ent)
{
	return memset(ent, 0, sizeof(*ent));
}

int rtud_init_exports(void);
void rtud_handle_wripc(void);

#endif /*__WHITERABBIT_RTU_H*/

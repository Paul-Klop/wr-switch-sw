/*
 * White Rabbit RTU (Routing Table Unit)
 * Copyright (C) 2010, CERN.
 *
 * Version:     wrsw_rtud v1.0
 *
 * Authors:     Juan Luis Manas (juan.manas@integrasys.es)
 *              Miguel Baizan   (miguel.baizan@integrasys.es)
 *              Maciej Lipinski (maciej.lipinski@cern.ch)
 *
 * Description: RTU Filtering database header.
 *              Filtering database management related operations and filtering
 *              database mirror. Note there is a single Filtering Database
 *              object per Bridge (See 802.1Q - 12.7.1)
 *
 * Fixes:
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

#ifndef __WHITERABBIT_RTU_FD_H
#define __WHITERABBIT_RTU_FD_H

#include "rtu.h"

// Tells the rtu_fd_create_entry() function what to do if MAC entry is added
// and the same MAC is already known to be at some port (coud be the same or different).
// Most of the time we would like to override the entry because the device simply moved
// to different "location" (port) but if we want to have redundant connection, we need to 
// actually add this port to the entry and risk having loop in the network (if the TRU is
// not ON). On the other hand, if a static entry exists, we might not want to override
// it by dynamic entry.
#define OVERRIDE_EXISTING  0        // either dynamic or static (by config)
#define ADD_TO_EXISTING    1        // to be used for redundancy (with caution)
#define OVERRIDE_EXISTING_DYNAMIC 2 // but not static (by learning)

int rtu_fd_init(uint16_t poly, unsigned long aging)
    __attribute__ ((warn_unused_result));

int rtu_fd_create_entry(uint8_t mac[ETH_ALEN],
			uint16_t vid,
			uint32_t port_map,
			int dynamic,
			int at_existing_entry)
    __attribute__ ((warn_unused_result));

int rtu_fd_set_aging_time(unsigned long t) __attribute__ ((warn_unused_result));
void rtu_fd_set_hash_poly(uint16_t poly);
void rtu_fd_flush(void);
void rtu_fd_clear_entries_for_port(int dest_port, int type);
int rtu_fd_remove_entry(uint8_t *mac, uint32_t port_mask, int type);

void rtu_fd_create_vlan_entry(int vid, uint32_t port_mask, uint8_t fid,
			      uint8_t prio, int has_prio, int prio_override,
			      int drop);

#endif /*__WHITERABBIT_RTU_FD_H*/

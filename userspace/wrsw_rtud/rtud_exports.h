/*
 * White Rabbit RTU (Routing Table Unit)
 * Copyright (C) 2010, CERN.
 *
 * Version:     wrsw_rtud v1.0
 *
 * Authors:     Tomasz Wlostowski (tomasz.wlostowski@cern.ch)
 *
 * Description: Dump the filtering database.
 *
 * Fixes:
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

#ifndef __RTUD_EXPORTS_H
#define __RTUD_EXPORTS_H

#include <stdint.h>
#include <minipc.h>

#define RTU_LEARNING_ENABLE 1
#define RTU_LEARNING_DISABLE 0
#define RTU_SET_LEARNING 1
#define RTU_GET_LEARNING 2

#define RTU_UNREC_ENABLE 1
#define RTU_UNREC_DISABLE 0
#define RTU_SET_UNREC 1
#define RTU_GET_UNREC 2

#define RTU_SET_HP_MASK 1
#define RTU_GET_HP_MASK 2

/* Export of a function to set remove entry in rtu */
struct minipc_pd rtud_export_clear_entries = {
	.name = "clear_entries",
	.retval = MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),
	.args = {
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int), /* port */
		 MINIPC_ARG_END,
		 },
};

/* Export of a function to add entry in rtu */
struct minipc_pd rtud_export_add_entry = {
	.name = "add_entry",
	.retval = MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),
	.args = {
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_STRING, char *), /* MAC */
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int), /* port mask */
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int), /* type */
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int), /* VID */
		 MINIPC_ARG_END,
		 },
};

/* Export of a function to remove entry in rtu */
struct minipc_pd rtud_export_remove_entry = {
	.name = "remove_entry",
	.retval = MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),
	.args = {
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_STRING, char *), /* MAC */
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int), /* port mask */
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int), /* type */
		 MINIPC_ARG_END,
		 },
};

/* Export of a function to get status or enable/disable of learning process in
 * rtu */
struct minipc_pd rtud_export_learning_process = {
	.name = "learning_process",
	.retval = MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),
	.args = {
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int), /* operation */
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int), /* enable/disable */
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int), /* port mask */
		 MINIPC_ARG_END,
		 },
};

/* Export of a function to get status or enable/disable of dropping packets when
 * the destination MAC is not matched */
struct minipc_pd rtud_export_unrec = {
	.name = "unrec",
	.retval = MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),
	.args = {
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int), /* operation */
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int), /* enable/disable */
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int), /* port mask */
		 MINIPC_ARG_END,
		 },
};


/* Export of a function to add vlan entry in rtu */
struct minipc_pd rtud_export_vlan_entry = {
	.name = "vlan_entry",
	.retval = MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),
	.args = {
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),	// 0: vid
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),	// 1: fid
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),	// 2: mask
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),	// 3: drop
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),	// 4: prio
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),	// 5: has_prio
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),	// 6: prio_override
		 MINIPC_ARG_END,
		 },
};

/* Export of a function to add vlan entry in rtu */
struct minipc_pd rtud_export_hp_mask = {
	.name = "hp_mask",
	.retval = MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int),
	.args = {
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int), /* operation */
		 MINIPC_ARG_ENCODE(MINIPC_ATYPE_INT, int), /* HP mask */
		 MINIPC_ARG_END,
		 },
};

#endif

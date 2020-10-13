/*
 * Copyright (c) 2014, CERN
 *
 * Author: Grzegorz Daniluk <grzegorz.daniluk@cern.ch>
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
 *
 */

#ifndef __WR_VLANS_H__
#define __WR_VLANS_H__

#define NPORTS 18

#define MINIPC_TIMEOUT 200

#define PORT_MASK(x) (1<<(x))

#define OPT_HELP 	'h'
#define OPT_CLEAR 3
#define OPT_LIST 4
#define OPT_P_PORT  10
#define OPT_P_QMODE 11
#define OPT_P_VID   12
#define OPT_P_PRIO  13
#define OPT_P_UNTAG 14
#define OPT_P_LIST  15
#define OPT_RTU_VID  20
#define OPT_RTU_FID  21
#define OPT_RTU_PMASK 22
#define OPT_RTU_DROP  23
#define OPT_RTU_PRIO  24
#define OPT_RTU_DEL   25
#define OPT_RTU_HP_MASK 30
#define OPT_FILE_READ 'f'

/* struct for ranges of VLAN sets used in dot-config */
struct vlan_sets {
	char *name;
	int min;
	int max;
};

struct s_port_vlans
{
	int  valid_mask;
	char pmode;
	char fix_prio;
	char prio_val;
	int  vid;
	int  untag_mask;
};

struct rtu_vlans_t
{
	int vid;
	int fid;
	int pmask;
	int drop;
	int prio;
	int has_prio;
	int prio_override;
	int flags;
	struct rtu_vlans_t *next;
};

#endif

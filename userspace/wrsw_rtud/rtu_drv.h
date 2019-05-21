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
 * Description: RTU driver module in user space.
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

#ifndef __WHITERABBIT_RTU_DRV_H
#define __WHITERABBIT_RTU_DRV_H

#include "rtu.h"

#define RTU_MFIFO_R0_DATA_SEL   0x00000000
#define RTU_MFIFO_R1_ADDR_MASK  0x0007FFFF

#define RTU_DEVNAME             "/dev/wr_rtu"

int rtu_init(void)
    __attribute__ ((warn_unused_result));
void rtu_exit(void);

// UFIFO

int rtu_ufifo_is_empty(void);
int rtu_read_learning_queue_cnt(void);
int rtu_read_learning_queue(struct rtu_request *req);

// HTAB access

void rtu_write_htab_entry(uint16_t zbt_addr, struct rtu_filtering_entry *ent,
			  int flush);
void rtu_clean_htab(void);

// AGING - HTAB

void rtu_read_aging_bitmap(uint32_t * bitmap);

// VLAN TABLE

void rtu_write_vlan_entry(int vid, struct rtu_vlan_table_entry *ent);
void rtu_clean_vlan_entry(int vid);
void rtu_clean_vlan(void);

// GLOBAL CONTROL REGISTER

void rtu_enable(void);
void rtu_disable(void);
void rtu_write_hash_poly(uint16_t hash_poly);
uint16_t rtu_read_hash_poly(void);
void rtu_set_active_htab_bank(uint8_t bank);
void rtu_set_active_hcam_bank(uint8_t bank);
void rtu_set_active_bank(uint8_t bank);

// PORT SETTINGS

int rtu_set_fixed_prio_on_port(int port, uint8_t prio)
    __attribute__ ((warn_unused_result));
int rtu_unset_fixed_prio_on_port(int port)
    __attribute__ ((warn_unused_result));
int rtu_learn_enable_on_port(int port, int flag)
    __attribute__ ((warn_unused_result));
int rtu_learn_read_on_port(int port);
int rtu_pass_bpdu_on_port(int port, int flag)
    __attribute__ ((warn_unused_result));
int rtu_pass_all_on_port(int port, int pass_all)
    __attribute__ ((warn_unused_result));
int rtu_set_unrecognised_behaviour_on_port(int port, int flag)
    __attribute__ ((warn_unused_result));
int rtu_read_unrecognised_behaviour_on_port(int port);

// IRQs

void rtu_enable_irq(void);
void rtu_disable_irq(void);
void rtu_clear_irq(void);

// PORT MIRRORING
int rtu_enable_mirroring(int ena);
int rtu_cfg_mirroring(struct rtu_mirror_info *cfg);
int rtu_get_mirroring(int *en, uint32_t *imask, uint32_t *emask, uint32_t *dmask);

#endif /*__WHITERABBIT_RTU_DRV_H*/

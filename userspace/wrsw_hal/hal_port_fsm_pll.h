/*
 * Copyright (C) 2019 CERN (www.cern.ch)
 * Author: Jean-Claude BAU - CERN
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 *
 *
 */

#ifndef HAL_PORT_FSM_PLL_H
#define HAL_PORT_FSM_PLL_H

/* prototypes */
void hal_port_pll_init_fsm(struct hal_port_state * ps );
int  hal_port_pll_state_fsm( struct hal_port_state * ps );

#endif

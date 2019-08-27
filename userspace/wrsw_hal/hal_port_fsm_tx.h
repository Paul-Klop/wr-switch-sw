/*
 * Copyright (C) 2019 CERN (www.cern.ch)
 * Author: Jean-Claude BAU - CERN
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 *
 *
 */

#ifndef HAL_PORT_FSM_TX_H
#define HAL_PORT_FSM_TX_H

/* prototypes */
void hal_port_tx_setup_init_fsm(struct hal_port_state * ps );
void hal_port_tx_setup_init(struct hal_port_state * ps );
int  hal_port_tx_setup_state_fsm( struct hal_port_state * ps );

#endif

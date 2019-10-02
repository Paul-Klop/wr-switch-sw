/*
 * Copyright (C) 2019 CERN (www.cern.ch)
 * Author: Jean-Claude BAU - CERN
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 *
 *
 */

#ifndef HAL_PORT_FSM_RX_H
#define HAL_PORT_FSM_RX_H

/* prototypes */
void hal_port_rx_setup_fsm_init(struct hal_port_state * ps );
int  hal_port_rx_setup_fsm_run( struct hal_port_state * ps );
void hal_port_rx_setup_fsm_reset(struct hal_port_state * ps );
void hal_port_rx_setup_init_all(struct hal_port_state * ports);


#endif

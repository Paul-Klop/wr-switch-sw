/*
 * Copyright (C) 2019 CERN (www.cern.ch)
 * Author: Jean-Claude BAU - CERN
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 *
 *
 */

#ifndef HAL_PORT_FSM_H
#define HAL_PORT_FSM_H

#define HAL_CAL_DMTD_SAMPLES 16
#define HAL_DEFAULT_DMTD_SAMPLES 512

/* Prototypes */
void fsm_state_machine( struct hal_port_state * ps );
void hal_port_state_fsm( struct hal_port_state * ps );
void hal_port_state_fsm_init( struct hal_port_state * ps );

#endif

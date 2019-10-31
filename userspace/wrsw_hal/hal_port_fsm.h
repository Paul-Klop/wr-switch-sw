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

void hal_port_state_fsm_init_all( struct hal_port_state * ports, halGlobalLPDC_t *globalLpdc);
void hal_port_state_fsm_run_all( struct hal_port_state * ports);

#endif

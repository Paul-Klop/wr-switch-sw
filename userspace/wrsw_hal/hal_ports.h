/*
 * Copyright (C) 2019 CERN (www.cern.ch)
 * Author: Jean-Claude BAU - CERN
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 *
 *
 */

#ifndef HAL_PORTS_H
#define HAL_PORTS_H

#include <rt_ipc.h>

typedef struct {
	struct hal_port_state *ports;
	int numberOfPorts;
	int hal_port_fd; /* An fd of always opened raw sockets for ioctl()-ing Ethernet devices */

	/* RT subsystem PLL state, polled regularly via mini-ipc */
	struct rts_pll_state rts_state;
	int rts_state_valid;

}hal_ports_t;


#define isRtsStateValid() halPorts.rts_state_valid
#define setRtsStateValidity(value) halPorts.rts_state_valid=(value)

#define getRtsState()   (halPorts.rts_state)
#define getRtsStatePtr()   (&getRtsState())

extern hal_ports_t halPorts;

extern int hal_port_poll_rts_state(void);
extern int hal_port_poll_rts_state(void);
extern int hal_get_timing_mode(void);
extern int rts_lock_channel(int channel, int priority);

#endif

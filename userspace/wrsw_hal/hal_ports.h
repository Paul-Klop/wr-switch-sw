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

typedef void (*hal_cleanup_callback_t)(void);


#define isRtsStateValid() halPorts.rts_state_valid
#define setRtsStateValidity(value) halPorts.rts_state_valid=(value)

#define getRtsState()   (halPorts.rts_state)
#define getRtsStatePtr()   (&getRtsState())

extern hal_ports_t halPorts;

extern int hal_port_poll_rts_state(void);
extern int hal_port_start_lock(const char *port_name, int priority);
extern int hal_port_enable_tracking(const char *port_name);
extern int hal_port_check_lock(const struct hal_port_state *ps);
extern int hal_port_check_lock_by_name(const char *port_name);
extern int hal_port_reset(const char *port_name);
extern int hal_port_pshifter_busy(void);
extern void hal_port_update_info(char *iface_name, int mode, int synchronized);
extern void hal_port_update_all(void);
extern int hal_port_shmem_init(char *logfilename);
extern int hal_port_wripc_init(char *logfilename);
extern int rts_lock_channel(int channel, int priority);

extern int hal_wripc_init(struct hal_port_state *hal_ports, char *logfilename);
extern int hal_wripc_update(int ms_timeout);

extern int hal_check_running(void);

extern int hal_add_cleanup_callback(hal_cleanup_callback_t cb);
extern int pcs_writel(struct hal_port_state *p, uint16_t value, int reg);
extern int pcs_readl(struct hal_port_state * p, int reg, uint32_t *value);


#endif

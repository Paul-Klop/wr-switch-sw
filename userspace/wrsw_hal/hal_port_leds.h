/*
 * Copyright (C) 2019 CERN (www.cern.ch)
 * Author: Jean-Claude BAU - CERN
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 *
 *
 */

#ifndef HAL_PORT_LEDS_H
#define HAL_PORT_LEDS_H

/* Prototypes */

extern void led_init_all_ports(struct hal_port_state *ps );
extern void led_set_wrmode(int portIndex, int val);
extern void led_link_update(struct hal_port_state *port);
extern void led_set_synched(int portIndex, int val);
extern void led_sync_update(struct hal_port_state *port );

#endif

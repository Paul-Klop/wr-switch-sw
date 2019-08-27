/*
 * Copyright (C) 2019 CERN (www.cern.ch)
 * Author: Jean-Claude BAU - CERN
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 *
 *
 */

#include <hal_exports.h>
#include <libwr/hal_shmem.h>
#include "hal_ports.h"
#include "hal_port_leds.h"

static struct Leds {
	unsigned char sync_leds_map_to_update[HAL_MAX_PORTS];
	unsigned char link_leds_map_to_update[HAL_MAX_PORTS];
	unsigned char sync_leds_map[HAL_MAX_PORTS];
	unsigned char link_leds_map[HAL_MAX_PORTS];
} _leds;

static int led_blink_state = 0;

/* flip state of the leds to blink */
void led_blink_state_change(void) {
	led_blink_state = 1 - led_blink_state;
}

/* state of blinking led (on/off) */
int led_get_blink_state(void) {
	return led_blink_state;
}

void led_init_all_ports(struct hal_port_state *ps ) {
	int i;

	// Clear data
	memset(&_leds,0,sizeof(_leds));

	for (i = 0; i < HAL_MAX_PORTS; i++) {
		shw_sfp_set_led_synced(i, 0);
		shw_sfp_set_generic(i, 0, SFP_LED_WRMODE1 | SFP_LED_WRMODE2);
	}
}

/* to avoid i2c transfers to set the link LEDs, cache their state */
void led_set_wrmode(int portIndex, int value)
{

	if (portIndex >= HAL_MAX_PORTS)
		return;

	_leds.link_leds_map_to_update[portIndex]=value;

}

void led_link_update(struct hal_port_state *ps) {
	int i;

	for (i = 0; i < HAL_MAX_PORTS; i++) {

		unsigned char value=_leds.link_leds_map_to_update[i];
		if ( value != _leds.link_leds_map[i] ||
		     value == SFP_LED_WRMODE_TX_CALIB /* always update to blink*/) {
			_leds.link_leds_map[i]=value;

			/* update the LED, don't forget to turn off LEDs if needed */
			switch (value) {
			case SFP_LED_WRMODE_SLAVE :
				/* cannot set and clear LED in the same call! */
				shw_sfp_set_generic(i, 1, SFP_LED_WRMODE1);
				shw_sfp_set_generic(i, 0, SFP_LED_WRMODE2);
				break;
			case SFP_LED_WRMODE_OTHER :
				/* cannot set and clear LED in the same call! */
				shw_sfp_set_generic(i, 0, SFP_LED_WRMODE1);
				shw_sfp_set_generic(i, 1, SFP_LED_WRMODE2);
				break;
			case SFP_LED_WRMODE_MASTER:
				shw_sfp_set_generic(i, 1,SFP_LED_WRMODE1 | SFP_LED_WRMODE2);
				break;
                        case SFP_LED_WRMODE_TX_CALIB:
				if(led_get_blink_state()){ // SFP_LED_WRMODE_OTHER
					shw_sfp_set_generic(i, 0, SFP_LED_WRMODE1);
					shw_sfp_set_generic(i, 1, SFP_LED_WRMODE2);
				}
				else { // SFP_LED_WRMODE_OFF
					shw_sfp_set_generic(i, 0,
					SFP_LED_WRMODE1 | SFP_LED_WRMODE2);
                                }
				break;
			case SFP_LED_WRMODE_OFF :
				shw_sfp_set_generic(i, 0,
				SFP_LED_WRMODE1 | SFP_LED_WRMODE2);
				break;
			}
		}

#if 0
		if (port->in_use && state_up(port->state)) {

			if (port->portMode == PORT_MODE_SLAVE)
				set_led_wrmode(i, SFP_LED_WRMODE_SLAVE);
			else if (port->portMode  == PORT_MODE_MASTER)
				set_led_wrmode(i, SFP_LED_WRMODE_MASTER);
			else
				set_led_wrmode(i, SFP_LED_WRMODE_OTHER);
		}
		port++;
#endif
	}
	led_blink_state_change();
}

/* to avoid i2c transfers to set the synced LEDs, cache their state */
 void led_set_synched(int portIndex, int value)
{
	if (portIndex >= HAL_MAX_PORTS)
		return;

	_leds.sync_leds_map_to_update[portIndex] = value;
}


void led_synched_update(struct hal_port_state *ps )
{
	int i;

	for (i = 0; i < HAL_MAX_PORTS; i++) {
#if 0
		/* Check:
		 * --port in use
		 * --link is up
		 */
		if (  ps->in_use
		    && state_up(ps->state) ) {

			int ledValue;

			ledValue= ps->synchronized
			    && (ps->portInfoUpdated--) > -10
			    ? 1 : 0;
			set_led_synced(i, ledValue);
		}
#else
		unsigned char value=_leds.sync_leds_map_to_update[i];
		if ( value != _leds.sync_leds_map[i]) {
			// Update led
			shw_sfp_set_led_synced(i, (int)value);
			_leds.sync_leds_map[i] = value;
		}
		ps++;
#endif

	}
}


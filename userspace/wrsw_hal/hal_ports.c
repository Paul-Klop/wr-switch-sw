/* Port initialization and state machine */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/if.h>

/* LOTs of hardware includes */
#include <rt_ipc.h>
#include <libwr/switch_hw.h>
#include <libwr/wrs-msg.h>
#include <libwr/pio.h>
#include <libwr/sfp_lib.h>
#include <libwr/shmem.h>
#include <libwr/config.h>
#include <libwr/timeout.h>
#include <libwr/hal_shmem.h>

#include <ppsi/ppsi.h>
#include "driver_stuff.h"
#include "hal_exports.h"
#include "hal_timer.h"
#include "hal_port_fsm.h"
#include "hal_port_fsm_tx.h"
#include "hal_port_leds.h"
#include "hal_ports.h"
#include "hal_timing.h"

extern struct hal_shmem_header *hal_shmem;
extern struct wrs_shm_head *hal_shmem_hdr;

hal_ports_t halPorts;

/**
 * End new stuff
 */

typedef enum {
	TMO_PORT_TMO_RTS=0,
	TMO_PORT_POLL_SFP,
	TMO_PORT_SFP_DOM,
	TMO_UPDATE_SYNC_LEDs,
	TMO_UPDATE_LINK_LEDS,
	TMO_COUNT
}port_tmo_id_t;

static void _cb_port_poll_rts_state(int timerId);
static void _cb_port_poll_sfp(int timerId);
static void _cb_port_poll_sfp_dom(int timerId);
static void _cb_port_update_sync_leds(int timerId);
static void _cb_port_update_link_leds(int timerId);


/* Polling timeouts (RT Subsystem & SFP detection) */
static timer_parameter_t _timerParameters[] = {
		{
				.id=TMO_PORT_TMO_RTS,
				.tmoMs=250, // 250 ms
				.repeat=1,
				.cb=_cb_port_poll_rts_state
		},
		{
				.id=TMO_PORT_POLL_SFP,
				.tmoMs=1000, // 1s
				.repeat=1,
				.cb=_cb_port_poll_sfp
		},
		{
				.id=TMO_PORT_SFP_DOM,
				.tmoMs=1000, // 1s
				.repeat=1,
				.cb=_cb_port_poll_sfp_dom
		},
		{
				.id=TMO_UPDATE_SYNC_LEDs,
				.tmoMs= 500, // 500ms
				.repeat=1,
				.cb=_cb_port_update_sync_leds
		},
		{
				.id=TMO_UPDATE_LINK_LEDS,
				.tmoMs=500, // 500ms
				.repeat=1,
				.cb=_cb_port_update_link_leds
		},
};

#define PORT_TIMER_COUNT (sizeof(_timerParameters)/sizeof(timer_parameter_t))

/* prototypes */
static int hal_port_check_lpdc_support(struct hal_port_state * ps);

/* checks if the port is supported by the FPGA firmware */
static int hal_port_check_presence(const char *if_name, unsigned char *mac)
{
	struct ifreq ifr;

	strncpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name));

	if (ioctl(halPorts.hal_port_fd, SIOCGIFHWADDR, &ifr) < 0)
		return 0;
	memcpy(mac, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
	return 1;
}

/* Port initialization, from dot-config values */
static int hal_port_init(struct hal_port_state *ps, int index)
{
	char key[128];
	int port_i;
	char *retValue;
	int maxFibers;

	/* index is 0..17, port_i 1..18 */
	port_i = index + 1;

	/* read dot-config values to get the interface name */
	sprintf(key,"PORT%02i_IFACE",port_i);
	if( (retValue=libwr_cfg_get(key))==NULL)
		return -1;
	strncpy(ps->name, retValue, 16);

	/* check if the port is built into the firmware, if not, we are done */
	if (!hal_port_check_presence(ps->name, ps->hw_addr))
		return -1;

	ps->in_use = 1;
	ps->lpdc.isSupported = hal_port_check_lpdc_support(ps);

	if ( ps->lpdc.isSupported ) {
		// Allocate memory for tx/rx setup
		ps->lpdc.txSetup = malloc(sizeof(halPortLpdcTx_t));
		ps->lpdc.rxSetup = malloc(sizeof(halPortLpdcRx_t));
	}
	/* get the number of a port from notation wriX */
	sscanf(ps->name + 3, "%d", &ps->hw_index);
	/* hw_index is 0..17, p->name wri1..18 */
	ps->hw_index--;

	/* Get fiber type */
	ps->fiber_index = 0; /* Default fiber value */
	sprintf(key,"PORT%02i_FIBER",port_i);
	if( (retValue=libwr_cfg_get(key))==NULL ) {
		pr_error("port %i (%s): no key \"%s\" specified. Default fiber 0\n",
			port_i, ps->name,key);
	} else {
		if (sscanf(retValue, "%i", &ps->fiber_index) != 1) {
			pr_error("port %i (%s): Invalid key \"%s\" value (%d). Default fiber 0\n",
				port_i, ps->name, key,*retValue);
		}
	}
	/* read dot-config values to get the number of defined fibers */
	strcpy(key,"N_FIBER_ENTRIES");
	if( (retValue=libwr_cfg_get(key))==NULL) {
		pr_error("port %i (%s): no key \"%s\" specified\n",
			port_i, ps->name,key);
		maxFibers=-1;
	} else
		if (sscanf(retValue, "%i", &maxFibers) != 1) {
			pr_error("Invalid key \"%s\" value (%d)\n",key,*retValue);
			maxFibers=-1;
		}

	if (ps->fiber_index > maxFibers) {
		pr_error("port %i (%s): "
			"not supported fiber value (%d), default to 0\n",
			port_i, ps->name,ps->fiber_index);
		ps->fiber_index = 0;
	}

	/* Enable port monitoring by default */
	ps->monitor = HAL_PORT_MONITOR_ENABLE;
	sprintf(key,"PORT%02i_INST%02i_MONITOR",port_i,1);
	if ((retValue = libwr_cfg_get(key)) == NULL ) {
		pr_error("port %i (%s): no key \"%s\" specified. Default to"
			 " monitor=y.\n",
			 port_i, ps->name,key);
	} else {
		if (!strcasecmp(retValue, "n")) {
			ps->monitor = HAL_PORT_MONITOR_DISABLE;
			pr_info("port %i (%s): monitor=n (%i)\n", port_i,
				ps->name, ps->monitor);
		} else if (!strcasecmp(retValue, "y")) {
			ps->monitor = HAL_PORT_MONITOR_ENABLE;
			pr_info("port %i (%s): monitor=y (%i)\n", port_i,
				ps->name, ps->monitor);
		} else {
			/* error */
			pr_error("port %i (%s): not supported \"monitor\" "
				 "value, default to y\n",
				 port_i, ps->name);
		}
	}

	/* FIXME: this address should come from the driver header */
	ps->ep_base = 0x30000 + 0x400 * ps->hw_index;

	return 0;
}

/* Interates via all the ports defined in the config file and
 * intializes them one after another. */
int hal_port_shmem_init(char *logfilename)
{
	int index;
	char *ret;
	pr_info("Initializing switch ports...\n");

	/* default timeouts */
	timer_init(_timerParameters,PORT_TIMER_COUNT);

	/* Open a single raw socket for accessing the MAC addresses, etc. */
	halPorts.hal_port_fd = socket(AF_PACKET, SOCK_DGRAM, 0);
	if (halPorts.hal_port_fd < 0) {
		pr_error("Can't create socket: %s\n", strerror(errno));
		return -1;
	}
	/* Allocate the ports in shared memory, so wr_mon etc can see them
	   Use lock since some (like rtud) wait for hal to be available */
	hal_shmem_hdr = wrs_shm_get(wrs_shm_hal, "wrsw_hal",
				WRS_SHM_WRITE | WRS_SHM_LOCKED);
	if (!hal_shmem_hdr) {
		pr_error("Can't join shmem: %s\n", strerror(errno));
		return -1;
	}
	hal_shmem = wrs_shm_alloc(hal_shmem_hdr, sizeof(*hal_shmem));
	halPorts.ports = wrs_shm_alloc(hal_shmem_hdr,
			      sizeof(struct hal_port_state)
			      * HAL_MAX_PORTS);
	if (!hal_shmem || !halPorts.ports) {
		pr_error("Can't allocate in shmem\n");
		return -1;
	}

	hal_shmem->ports = halPorts.ports;

	for (index = 0; index < HAL_MAX_PORTS; index++)
		if (hal_port_init(&halPorts.ports[index],index) < 0)
			break;
	hal_port_state_fsm_init(halPorts.ports); // Init fsm
	hal_port_tx_setup_init(halPorts.ports); // Global init for tx_setup
	led_init_all_ports(halPorts.ports); // Reset all leds
	halPorts.numberOfPorts = index;

	pr_info("Number of physical ports supported in HW: %d\n",
			halPorts.numberOfPorts );

	/* We are done, mark things as valid */
	hal_shmem->nports = halPorts.numberOfPorts ;
	hal_shmem->hal_mode = hal_tmg_get_mode();

	ret = libwr_cfg_get("READ_SFP_DIAG_ENABLE");
	if (ret && !strcmp(ret, "y")) {
		pr_info("Read SFP Diagnostic Monitoring enabled\n");
		hal_shmem->read_sfp_diag = READ_SFP_DIAG_ENABLE;
	} else
		hal_shmem->read_sfp_diag = READ_SFP_DIAG_DISABLE;

	hal_shmem_hdr->version = HAL_SHMEM_VERSION;
	/* Release processes waiting for HAL's to fill shm with correct data
	   When shm is opened successfully data in shm is still not populated!
	   Read data with wrs_shm_seqbegin and wrs_shm_seqend!
	   Especially for nports it is important */
	wrs_shm_write(hal_shmem_hdr, WRS_SHM_WRITE_END);

	return 0;
}

int pcs_writel(struct hal_port_state *ps, uint16_t value, int reg)
{
	struct ifreq ifr;
	uint32_t rv;

	strncpy(ifr.ifr_name, ps->name, sizeof(ifr.ifr_name));

	rv = NIC_WRITE_PHY_CMD(reg, value);
	ifr.ifr_data = (void *)&rv;
	if (ioctl(halPorts.hal_port_fd, PRIV_IOCPHYREG, &ifr) < 0)
	{
		pr_error("%s: ioctl failed writing at register adress %d\n",
				__func__,reg);
		return -1;
	};

	return 0;
}

int pcs_readl(struct hal_port_state * p, int reg, uint32_t *value)
{
	struct ifreq ifr;

	strncpy(ifr.ifr_name, p->name, sizeof(ifr.ifr_name));

	*value = NIC_READ_PHY_CMD(reg);
	ifr.ifr_data = (void *)value;
	if (ioctl(halPorts.hal_port_fd, PRIV_IOCPHYREG, &ifr) < 0) {
		pr_error("%s: ioctl failed reading register at address %d\n",
				__func__, reg);
		return -1;
	}

	*value=NIC_RESULT_DATA(*value);
	return 0;
}

static uint32_t ep_read(struct hal_port_state * p, int reg_addr, uint32_t *value)
{
	struct ifreq ifr;

	strncpy(ifr.ifr_name, p->name, sizeof(ifr.ifr_name));

	*value = reg_addr;
	ifr.ifr_data = (void *) value;
	pr_info("raw fd %d name %s\n", halPorts.hal_port_fd, ifr.ifr_name);
	if (ioctl(halPorts.hal_port_fd, PRIV_IOCREADREG, &ifr) < 0) {
		pr_error("%s: ioctl failed reading register at address %d\n", __func__,reg_addr);
		return -1;
	}
	pr_info("ep_read: reg %d data %x\n", reg_addr, *value);
	return 0;
}

/* checks if the port supports Low Phase Drift Calibration*/
static int hal_port_check_lpdc_support(struct hal_port_state * ps)
{

	uint32_t rv;

	if ( ep_read(ps, EP_ECR_ADDR,&rv)<0 ) {
		pr_error("Cannot detects supports for Low Phase Drift Calibration "
				"at port %s\n", ps->name);
		return 0;
	} else {
		if (rv & EP_ECR_FEAT_LPC) {
			pr_info("Supports for Low Phase Drift Calibration detected"
					"at port %s\n", ps->name);
			return 1;
		} else {
			pr_info("NO supports for Low Phase Drift Calibration detected"
					"at port %s\n", ps->name);
			return 0;
		}
	}
}

int hal_port_wripc_init(char *logfilename)
{
	/* Create a WRIPC server for HAL public API */
	return hal_wripc_init(halPorts.ports, logfilename);
}


int hal_port_pshifter_busy()
{
	struct rts_pll_state *hs = getRtsStatePtr();

	if (! isRtsStateValid() )
		return 1;

	if (hs->current_ref != REF_NONE) {
		int busy =
		    hs->channels[hs->current_ref].
		    flags & CHAN_SHIFTING ? 1 : 0;

		return busy;
	}

	return 1;
}

/* Updates the current value of the phase shift on a given
 * port. Called by the main update function regularly. */
int hal_port_poll_rts_state(void)
{
	struct rts_pll_state *hs = getRtsStatePtr();

	setRtsStateValidity( rts_get_state(hs) < 0 ? 0 : 1);
	if (! isRtsStateValid() )
		pr_warning("rts_get_state failure, weird...\n");
	return isRtsStateValid();
}


static void hal_port_insert_sfp(struct hal_port_state * ps)
{
	struct shw_sfp_header shdr;
	struct shw_sfp_caldata *cdata;
	char subname[48];
	int err;

	memset(&shdr, 0, sizeof(struct shw_sfp_header));
	memset(&ps->calib.sfp_dom_raw, 0, sizeof(struct shw_sfp_dom));
	err = shw_sfp_read_verify_header(ps->hw_index, &shdr);
	if (err == -2) {
		pr_error("%s SFP module not inserted. Failed to read SFP "
			 "configuration header\n", ps->name);
		return;
	} else if (err < 0) {
		pr_error("Failed to read SFP configuration header for %s\n",
			 ps->name);
		/* Clear any mess that was wrongly read to make sure that
		   the SFP will be unrecognized. */
		memset(&shdr, 0, sizeof(struct shw_sfp_header));
	}
	memcpy(&ps->calib.sfp_header_raw, &shdr, sizeof(struct shw_sfp_header));

	if (hal_shmem->read_sfp_diag == READ_SFP_DIAG_ENABLE
	    && shdr.diagnostic_monitoring_type & SFP_DIAGNOSTIC_IMPLEMENTED) {
		pr_info("SFP Diagnostic Monitoring implemented in SFP plugged"
			" to port %d (%s)\n", ps->hw_index + 1, ps->name);
		if (shdr.diagnostic_monitoring_type & SFP_ADDR_CHANGE_REQ) {
			pr_warning("SFP in port %d (%s) requires special "
				   "address change before accessing Diagnostic"
				   " Monitoring, which is not implemented "
				   "right now\n", ps->hw_index + 1, ps->name);
		} else {
			/* copy coontent of SFP's Diagnostic Monitoring */
			shw_sfp_read_dom(ps->hw_index, &ps->calib.sfp_dom_raw);
			if (err < 0) {
				pr_error("Failed to read SFP Diagnostic "
					 "Monitoring for port %d (%s)\n",
					 ps->hw_index + 1, ps->name);
			}
			ps->has_sfp_diag = 1;
		}

	}
	pr_info("SFP Info: Manufacturer: %.16s P/N: %.16s, S/N: %.16s\n",
	      shdr.vendor_name, shdr.vendor_pn, shdr.vendor_serial);
	cdata = shw_sfp_get_cal_data(ps->hw_index, &shdr);
	if (cdata) {
		/* Alpha is not known now. It is read later from the fibers'
		 * database. */
		pr_info("%s SFP Info: (%s) delta Tx %d, delta Rx %d, "
			"TX wl: %dnm, RX wl: %dnm\n", ps->name,
			cdata->flags & SFP_FLAG_CLASS_DATA
			? "class-specific" : "device-specific",
			cdata->delta_tx_ps, cdata->delta_rx_ps, cdata->tx_wl,
			cdata->rx_wl);

		memcpy(&ps->calib.sfp, cdata,
		       sizeof(struct shw_sfp_caldata));
		/* Mark SFP as found in data base */
		ps->calib.sfp.flags |= SFP_FLAG_IN_DB;
	} else {
		pr_error("Unknown SFP vn=\"%.16s\" pn=\"%.16s\" "
			"vs=\"%.16s\" on port %s\n", shdr.vendor_name,
			shdr.vendor_pn, shdr.vendor_serial, ps->name);
		memset(&ps->calib.sfp, 0, sizeof(ps->calib.sfp));
	}

	ps->sfpPresent=1;
	shw_sfp_set_tx_disable(ps->hw_index, 0);
	/* Copy the strings anyways, for informative value in shmem */
	strncpy(ps->calib.sfp.vendor_name, (void *)shdr.vendor_name, 16);
	strncpy(ps->calib.sfp.part_num, (void *)shdr.vendor_pn, 16);
	strncpy(ps->calib.sfp.vendor_serial, (void *)shdr.vendor_serial, 16);
	/* check if SFP is 1GbE */
	ps->calib.sfp.flags |= shdr.br_nom == SFP_SPEED_1Gb ? SFP_FLAG_1GbE : 0;
	ps->calib.sfp.flags |= shdr.br_nom == SFP_SPEED_1Gb_10 ? SFP_FLAG_1GbE : 0;

	/*
	 * Now, we should fix the alpha value according to fiber
	 * type. Alpha does not depend on the SFP, but on the
	 * speed ratio of the SFP frequencies over the specific
	 * fiber. Thus, rely on the fiber type for this port.
	 */
	sprintf(subname, "alpha_%i_%i", ps->calib.sfp.tx_wl, ps->calib.sfp.rx_wl);
	err = libwr_cfg_convert2("FIBER%02i_PARAMS", subname,
				 LIBWR_DOUBLE, &ps->calib.sfp.alpha,
				 ps->fiber_index);
	if (!err) {
		/* Now we know alpha, so print it. */
		pr_info("%s SFP Info: alpha %.3f (* 1e6) found for TX wl: %dnm,"
			" RX wl: %dmn\n", ps->name, ps->calib.sfp.alpha * 1e6,
			ps->calib.sfp.tx_wl, ps->calib.sfp.rx_wl);
		return;
	}

	/* Try again, with the opposite direction (rx/tx) */
	sprintf(subname, "alpha_%i_%i", ps->calib.sfp.rx_wl, ps->calib.sfp.tx_wl);
	err = libwr_cfg_convert2("FIBER%02i_PARAMS", subname,
				 LIBWR_DOUBLE, &ps->calib.sfp.alpha,
				 ps->fiber_index);
	if (!err) {
		ps->calib.sfp.alpha = (1.0 / (1.0 + ps->calib.sfp.alpha)) - 1.0;
		/* Now we know alpha, so print it. */
		pr_info("%s SFP Info: alpha %.3f (* 1e6) found for TX wl: %dnm,"
			" RX wl: %dmn\n", ps->name, ps->calib.sfp.alpha * 1e6,
			ps->calib.sfp.tx_wl, ps->calib.sfp.rx_wl);
		return;
	}

	pr_error("Port %s, SFP vn=\"%.16s\" pn=\"%.16s\" vs=\"%.16s\", "
		"fiber %i: no alpha known\n", ps->name,
		ps->calib.sfp.vendor_name, ps->calib.sfp.part_num,
		ps->calib.sfp.vendor_serial, ps->fiber_index);
	ps->calib.sfp.alpha = 0;
}

static void hal_port_remove_sfp(struct hal_port_state * ps)
{
	/* clean SFP's details when removing SFP */
	memset(&ps->calib.sfp, 0, sizeof(ps->calib.sfp));
	memset(&ps->calib.sfp_header_raw, 0, sizeof(struct shw_sfp_header));
	memset(&ps->calib.sfp_dom_raw, 0, sizeof(struct shw_sfp_dom));
	ps->has_sfp_diag=ps->sfpPresent=0;
}

/* detects insertion/removal of SFP transceivers */
static void hal_port_poll_sfp(void)
{
	static int __old_mask = 0;
	uint32_t mask = shw_sfp_module_scan();

	if (mask != __old_mask) {
		int i, hw_index;

		/* lock shmem */
		wrs_shm_write(hal_shmem_hdr, WRS_SHM_WRITE_BEGIN);

		for (i = 0; i < HAL_MAX_PORTS; i++) {
			hw_index = halPorts.ports[i].hw_index;

			if (halPorts.ports[i].in_use
				&& (mask ^ __old_mask) & (1 << hw_index)) {
				int insert = mask & (1 << hw_index);
				pr_info("SFP Info: Detected SFP %s "
					  "on port %s.\n",
					  insert ? "insertion" : "removal",
							  halPorts.ports[i].name);
				if (insert)
					hal_port_insert_sfp(&halPorts.ports[i]);
				else
					hal_port_remove_sfp(&halPorts.ports[i]);
			}
		}

		/* unlock shmem */
		wrs_shm_write(hal_shmem_hdr, WRS_SHM_WRITE_END);
		__old_mask = mask;
	}
}

static void _cb_port_poll_rts_state(int timerId){
	/* poll_rts_state does not write to shmem */
	hal_port_poll_rts_state();
	// Update timing mode
	hal_shmem->hal_mode = hal_tmg_get_mode();
}

static void _cb_port_poll_sfp(int timerId){
	hal_port_poll_sfp();
}

static void _cb_port_poll_sfp_dom(int timerId){
	if (hal_shmem->read_sfp_diag == READ_SFP_DIAG_ENABLE) {
		struct shw_sfp_dom sfp_dom_raw[HAL_MAX_PORTS];
		struct hal_port_state *ps;
		int i;

		/* get the DOM data to local memory */
		ps=halPorts.ports;
		for (i = 0; i < HAL_MAX_PORTS; i++) {
			/* read DOM only for plugged ports with DOM
			 * capabilities */
			if (ps->in_use
			    && ps->sfpPresent
			    && ps->has_sfp_diag) {
				shw_sfp_update_dom(ps->hw_index,
						   &sfp_dom_raw[i]);
			}
			ps++;
		}

		/* lock shmem */
		wrs_shm_write(hal_shmem_hdr, WRS_SHM_WRITE_BEGIN);

		/* copy the DOM from local memory to shmem */
		ps=halPorts.ports;
		for (i = 0; i < HAL_MAX_PORTS; i++) {
			/* update DOM only for plugged ports with DOM
			 * capabilities */
			if (ps->in_use
			    && ps->sfpPresent
			    &&  ps->has_sfp_diag) {
				memcpy(&halPorts.ports[i].calib.sfp_dom_raw,
				       &sfp_dom_raw[i],
				       sizeof(struct shw_sfp_dom));
			}
			ps++;
		}

		/* unlock shmem */
		wrs_shm_write(hal_shmem_hdr, WRS_SHM_WRITE_END);
	}
}

static void _cb_port_update_sync_leds(int timerId){
	/* update LEDs of synced ports */
	led_synched_update(halPorts.ports);
}

static void _cb_port_update_link_leds(int timerId){
	/* update color of the link LEDs */
	led_link_update(halPorts.ports);
}

/* Executes the port FSM for all ports. Called regularly by the main loop. */
void hal_port_update_all()
{
	timer_scan(_timerParameters,PORT_TIMER_COUNT);

	/* lock shmem */
	wrs_shm_write(hal_shmem_hdr, WRS_SHM_WRITE_BEGIN);

	hal_port_state_fsm(halPorts.ports);

	/* unlock shmem */
	wrs_shm_write(hal_shmem_hdr, WRS_SHM_WRITE_END);
}

int hal_port_enable_tracking(const char *port_name)
{
	const struct hal_port_state *ps = hal_lookup_port(halPorts.ports,
						  halPorts.numberOfPorts, port_name);

	if (!ps)
		return -1;
	return rts_enable_ptracker(ps->hw_index, 1); /* 0 or -1 already */
}

/* Triggers the locking state machine, called by the PTPd during the
 * WR link setup phase. */
int hal_port_start_lock(const char *port_name, int priority)
{
	struct hal_port_state *ps = hal_lookup_port(halPorts.ports, halPorts.numberOfPorts, port_name);

	if ( !ps )
		return -1; /* unknown port */

	ps->evt_lock=1;
	return 0;
}

/* Returns 1 if the port is locked, 0 if unlocked, -1 in case of error */
int hal_port_check_lock(const struct hal_port_state *ps)
{
	struct rts_pll_state *hs = getRtsStatePtr();

	if (!ps)
		return -1;

	if (!isRtsStateValid())
		return -1;

	if (hs->delock_count > 0)
		return -1;

	return ( hs->mode==RTS_MODE_BC &&
		hs->current_ref == ps->hw_index &&
		(hs->flags & RTS_DMTD_LOCKED) &&
		(hs->flags & RTS_REF_LOCKED));
}

/* Returns 1 if the port is locked */
int hal_port_check_lock_by_name(const char *port_name)
{
	const struct hal_port_state *ps = hal_lookup_port(halPorts.ports,
			halPorts.numberOfPorts, port_name);

	return hal_port_check_lock(ps);
}

int hal_port_reset(const char *port_name)
{
	struct hal_port_state *ps = hal_lookup_port(halPorts.ports,
			halPorts.numberOfPorts, port_name);

	if (!ps)
		return -1;

	ps->evt_reset=1;
	return 0;
}

void hal_port_update_info(char *iface_name, int mode, int synchronized){

	int i;
	struct hal_port_state *ps=halPorts.ports;

	if ( iface_name==NULL ) {
		pr_error("%s: Invalid iface_name parameter (NULL).\n",__func__);
		return;
	}

	/* TODO: Improve/implement handling of many instances on a single 
	   physical port. Currently, the PPSi instance with the greatest
	   index will override any preceeding instances on a give physical
	   port. Very likely, a table with counter of instances per physical
	   port could be done. If the counter is greater than 1, the arbiration
	   kicks in. In such case, we could follow what was implemente 
	   previously, namely an Instance in PTP SLAVE state has precedence.
	   TO BE DISCUSSED.
        */
	for (i = 0; i < HAL_MAX_PORTS; i++) {

		if (ps->in_use &&
				!strcmp(iface_name,ps->name) ) {

			ps->portMode=mode;
			ps->synchronized=synchronized;
			ps->portInfoUpdated=1;
			break;
		}
		ps++;
	}
}


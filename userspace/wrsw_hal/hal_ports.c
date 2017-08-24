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
#include <libwr/switch_hw.h>
#include <libwr/wrs-msg.h>
#include <libwr/pio.h>
#include <libwr/sfp_lib.h>
#include <libwr/shmem.h>
#include <libwr/config.h>
#include <libwr/timeout.h>

#include <ppsi/ppsi.h>
#include "wrsw_hal.h"
#include <rt_ipc.h>
#include <hal_exports.h>
#include <libwr/hal_shmem.h>
#include "driver_stuff.h"

#define UPDATE_RTS_PERIOD 250 /* ms */
#define UPDATE_SFP_PERIOD 1000 /* ms */
#define UPDATE_SYNC_LEDS_PERIOD 500 /* ms */
#define UPDATE_LINK_LEDS_PERIOD 500 /* ms */
#define UPDATE_SFP_DOM_PERIOD 1000 /* ms */

extern struct hal_shmem_header *hal_shmem;
extern struct wrs_shm_head *hal_shmem_hdr;

/* Port table: the only item which is not "hal_port_*", as it's much used */
static struct hal_port_state *ports;

/* An fd of always opened raw sockets for ioctl()-ing Ethernet devices */
static int hal_port_fd;

/* RT subsystem PLL state, polled regularly via mini-ipc */
static struct rts_pll_state hal_port_rts_state;
static int hal_port_rts_state_valid = 0;

/* Polling timeouts (RT Subsystem & SFP detection) */
static timeout_t hal_port_tmo_rts, hal_port_tmo_sfp;
static timeout_t update_sync_leds_tmo, update_link_leds_tmo;
static timeout_t update_sfp_dom_tmo;
static int hal_port_nports;

static struct wr_servo_state *ppsi_servo;
static struct wr_servo_state ppsi_servo_local;
static struct pp_instance *ppsi_instances;
static struct pp_instance ppsi_instances_local[PP_MAX_LINKS];
static struct wrs_shm_head *ppsi_head;

static int ppsi_nlinks;

int hal_port_check_lock(const char *port_name);
static void update_link_leds(void);
static void set_led_wrmode(int p_index, int val);
static void set_led_synced(int p_index, int val);
static void update_sync_leds(void);
static int read_servo(void);
static int try_open_ppsi_shmem(void);

int hal_port_any_locked(void)
{
	if (!hal_port_rts_state_valid)
		return -1;
	if (hal_port_rts_state.current_ref == REF_NONE)
		return -1;

	return hal_port_rts_state.current_ref;
}

/* Resets the state variables of a port and re-starts its state machines */
static void hal_port_reset_state(struct hal_port_state * p)
{
	p->calib.rx_calibrated = 0;
	p->calib.tx_calibrated = 0;
	p->locked = 0;
	p->state = HAL_PORT_STATE_LINK_DOWN;
	p->lock_state = 0;
	p->tx_cal_pending = 0;
	p->rx_cal_pending = 0;
}

/* checks if the port is supported by the FPGA firmware */
static int hal_port_check_presence(const char *if_name, unsigned char *mac)
{
	struct ifreq ifr;

	strncpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name));

	if (ioctl(hal_port_fd, SIOCGIFHWADDR, &ifr) < 0)
		return 0;
	memcpy(mac, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
	return 1;
}

/* Port initialization, from dot-config values */
static int hal_port_init(int index)
{
	struct hal_port_state *p = &ports[index];
	char name[128], s[128];
	int val, error;
	int port_i;

	/* index is 0..17, port_i 1..18 */
	port_i = index + 1;

	/* make sure the states and other variables are in their init state */
	hal_port_reset_state(p);

	/* read dot-config values for this index, starting from name */
	error = libwr_cfg_convert2("PORT%02i_PARAMS", "name", LIBWR_STRING,
				   name, port_i);
	if (error)
		return -1;
	strncpy(p->name, name, 16);

	/* check if the port is built into the firmware, if not, we are done */
	if (!hal_port_check_presence(name, p->hw_addr))
		return -1;

	p->state = HAL_PORT_STATE_DISABLED;
	p->in_use = 1;

	val = 18 * 800; /* magic default from previous code */
	error = libwr_cfg_convert2("PORT%02i_PARAMS", "tx", LIBWR_INT,
				   &val, port_i);
	if (error)
		pr_error("port %i (%s): no \"tx=\" specified\n",
			port_i, name);
	p->calib.phy_tx_min = val;

	val = 18 * 800; /* magic default from previous code */
	error = libwr_cfg_convert2("PORT%02i_PARAMS", "rx", LIBWR_INT,
				   &val, port_i);
	if (error)
		pr_error("port %i (%s): no \"rx=\" specified\n",
			port_i, name);
	p->calib.phy_rx_min = val;

	p->calib.delta_tx_board = 0; /* never set */
	p->calib.delta_rx_board = 0; /* never set */
	/* get the number of a port from notation wriX */
	sscanf(p->name + 3, "%d", &p->hw_index);
	/* hw_index is 0..17, p->name wri1..18 */
	p->hw_index--;

	p->t2_phase_transition = DEFAULT_T2_PHASE_TRANS;
	p->t4_phase_transition = DEFAULT_T4_PHASE_TRANS;
	p->clock_period = REF_CLOCK_PERIOD_PS;

	/* enabling of ports is done by startup script */

	{
		static struct roletab { char *name; int value; } *rp, rt[] = {
			{"auto",   HEXP_PORT_MODE_WR_M_AND_S},
			{"master", HEXP_PORT_MODE_WR_MASTER},
			{"slave",  HEXP_PORT_MODE_WR_SLAVE},
			{"non-wr", HEXP_PORT_MODE_NON_WR},
			{"none",   HEXP_PORT_MODE_NONE},
			{NULL,     HEXP_PORT_MODE_NON_WR /* default,
						* should exist and be last*/},
		};

		strcpy(s, "non-wr"); /* default if no string passed */
		p->mode = HEXP_PORT_MODE_NON_WR;
		error = libwr_cfg_convert2("PORT%02i_PARAMS", "role",
					   LIBWR_STRING, s, port_i);
		if (error)
			pr_error("port %i (%s): "
				"no \"role=\" specified\n", port_i, name);

		for (rp = rt; rp->name; rp++)
			if (!strcasecmp(s, rp->name))
				break;
		p->mode = rp->value;

		if (!rp->name) {
			for (rp = rt; rp->name; rp++)
				if (p->mode == rp->value)
					break;
			pr_error("port %i (%s): invalid role "
				"\"%s\" specified; using mode %s\n", port_i,
				name, s, rp->name);
		}

		pr_debug("Port %s: mode %s (%i)\n", p->name, rp->name,
			 p->mode);
	}

	/* Get fiber type */
	error = libwr_cfg_convert2("PORT%02i_PARAMS", "fiber",
				   LIBWR_INT, &p->fiber_index, port_i);

	if (error) {
		pr_error("port %i (%s): "
			"no \"fiber=\" specified, default fiber to 0\n",
			port_i, name);
		p->fiber_index = 0;
		}
	if (p->fiber_index > 3) {
		pr_error("port %i (%s): "
			"not supported \"fiber=\" value, default to 0\n",
			port_i, name);
		p->fiber_index = 0;
		}

	/* Used to pre-calibrate the TX path for each port. No more in V3 */

	/* FIXME: this address should come from the driver header */
	p->ep_base = 0x30000 + 0x400 * p->hw_index;

	return 0;
}

/* Interates via all the ports defined in the config file and
 * intializes them one after another. */
int hal_port_init_shmem(char *logfilename)
{
	int index;
	char *ret;
	pr_info("Initializing switch ports...\n");

	/* default timeouts */
	libwr_tmo_init(&hal_port_tmo_sfp, UPDATE_SFP_PERIOD, 1);
	libwr_tmo_init(&hal_port_tmo_rts, UPDATE_RTS_PERIOD, 1);
	libwr_tmo_init(&update_sync_leds_tmo, UPDATE_SYNC_LEDS_PERIOD, 1);
	libwr_tmo_init(&update_link_leds_tmo, UPDATE_LINK_LEDS_PERIOD, 1);
	libwr_tmo_init(&update_sfp_dom_tmo, UPDATE_SFP_DOM_PERIOD, 1);

	/* Open a single raw socket for accessing the MAC addresses, etc. */
	hal_port_fd = socket(AF_PACKET, SOCK_DGRAM, 0);
	if (hal_port_fd < 0) {
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
	ports = wrs_shm_alloc(hal_shmem_hdr,
			      sizeof(struct hal_port_state)
			      * HAL_MAX_PORTS);
	if (!hal_shmem || !ports) {
		pr_error("Can't allocate in shmem\n");
		return -1;
	}

	hal_shmem->ports = ports;

	for (index = 0; index < HAL_MAX_PORTS; index++)
		if (hal_port_init(index) < 0)
			break;

	hal_port_nports = index;

	pr_info("Number of physical ports supported in HW: %d\n",
	      hal_port_nports);

	/* We are done, mark things as valid */
	hal_shmem->nports = hal_port_nports;
	hal_shmem->hal_mode = hal_get_timing_mode();

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

int hal_port_init_wripc(char *logfilename)
{
	/* Create a WRIPC server for HAL public API */
	return hal_init_wripc(ports, logfilename);
}

/* Checks if the link is up on inteface (if_name). Returns non-zero if yes. */
static int hal_port_check_link(const char *if_name)
{
	struct ifreq ifr;

	strncpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name));

	if (ioctl(hal_port_fd, SIOCGIFFLAGS, &ifr) > 0)
		return -1;

	return (ifr.ifr_flags & IFF_UP && ifr.ifr_flags & IFF_RUNNING);
}

/* Port locking state machine - controls the HPLL/DMPLL.  TODO (v3):
   get rid of this code - this will all be moved to the realtime CPU
   inside the FPGA and the softpll. */
static void hal_port_locking_fsm(struct hal_port_state * p)
{
}

int hal_port_pshifter_busy()
{
	struct rts_pll_state *hs = &hal_port_rts_state;

	if (!hal_port_rts_state_valid)
		return 1;

	if (hs->current_ref != REF_NONE) {
		int busy =
		    hs->channels[hs->current_ref].
		    flags & CHAN_SHIFTING ? 1 : 0;

		if (0)
			pr_info("PSBusy %d, flags %x\n", busy,
			      hs->channels[hs->current_ref].flags);
		return busy;
	}

	return 1;
}

/* Updates the current value of the phase shift on a given
 * port. Called by the main update function regularly. */
static void poll_rts_state(void)
{
	struct rts_pll_state *hs = &hal_port_rts_state;

	if (libwr_tmo_expired(&hal_port_tmo_rts)) {
		hal_port_rts_state_valid = rts_get_state(hs) < 0 ? 0 : 1;
		if (!hal_port_rts_state_valid)
			printf("rts_get_state failure, weird...\n");
	}
}

static uint32_t pcs_readl(struct hal_port_state * p, int reg)
{
	struct ifreq ifr;
	uint32_t rv;

	strncpy(ifr.ifr_name, p->name, sizeof(ifr.ifr_name));

	rv = NIC_READ_PHY_CMD(reg);
	ifr.ifr_data = (void *)&rv;
//      printf("raw fd %d name %s\n", hal_port_fd, ifr.ifr_name);
	if (ioctl(hal_port_fd, PRIV_IOCPHYREG, &ifr) < 0) {
		pr_error("ioctl failed\n");
	};

//      printf("PCS_readl: reg %d data %x\n", reg, NIC_RESULT_DATA(rv));
	return NIC_RESULT_DATA(rv);
}

static int hal_port_link_down(struct hal_port_state * p, int link_up)
{
	/* If, at any moment, the link goes down, reset the FSM and
	 * the port state structure. */
	if (!link_up && p->state != HAL_PORT_STATE_LINK_DOWN
	    && p->state != HAL_PORT_STATE_DISABLED) {
		if (p->locked) {
			pr_info("Switching RTS to use local reference\n");
			if (hal_get_timing_mode()
			    != HAL_TIMING_MODE_GRAND_MASTER)
				rts_set_mode(RTS_MODE_GM_FREERUNNING);
		}

		/* turn off synced LED */
		set_led_synced(p->hw_index, 0);

		/* turn off link/wrmode LEDs */
		set_led_wrmode(p->hw_index, SFP_LED_WRMODE_OFF);
		p->state = HAL_PORT_STATE_LINK_DOWN;
		hal_port_reset_state(p);

		rts_enable_ptracker(p->hw_index, 0);
		pr_info("%s: link down\n", p->name);

		return 1;
	}
	return 0;
}

/* Main port state machine */
static void hal_port_fsm(struct hal_port_state * p)
{
	struct rts_pll_state *hs = &hal_port_rts_state;
	int link_up = hal_port_check_link(p->name);

	if (hal_port_link_down(p, link_up))
		return;
	/* handle the locking part */
	hal_port_locking_fsm(p);

	switch (p->state) {

	case HAL_PORT_STATE_DISABLED:
		p->calib.tx_calibrated = 0;
		p->calib.rx_calibrated = 0;
		break;

		/* Default state - wait until the link goes up */
	case HAL_PORT_STATE_LINK_DOWN:
	case HAL_PORT_STATE_RESET:
		{
			if (link_up) {
				p->calib.tx_calibrated = 1;
				p->calib.rx_calibrated = 1;
				/* FIXME: use proper register names */
				pr_info("Bitslide: %d\n",
				      ((pcs_readl(p, 16) >> 4) & 0x1f));
				p->calib.delta_rx_phy =
				    p->calib.phy_rx_min +
				    ((pcs_readl(p, 16) >> 4) & 0x1f) * 800;
				p->calib.delta_tx_phy = p->calib.phy_tx_min;

				if (0)
					pr_info(
					      "Bypassing calibration for "
					      "downlink port %s [dTx %d, dRx %d]\n",
					      p->name, p->calib.delta_tx_phy,
					      p->calib.delta_rx_phy);

				p->tx_cal_pending = 0;
				p->rx_cal_pending = 0;
				/* Set link/wrmode LEDs to other. Master/slave
				 * color is set in the different place */
				set_led_wrmode(p->hw_index,
					       SFP_LED_WRMODE_OTHER);
				pr_info("%s: link up\n", p->name);
				p->state = HAL_PORT_STATE_UP;
			}
			break;
		}

		/* Default "on" state - just keep polling the phase value. */
	case HAL_PORT_STATE_UP:
		if (hal_port_rts_state_valid) {
			p->phase_val =
			    hs->channels[p->hw_index].phase_loopback;
			p->phase_val_valid =
			    hs->channels[p->hw_index].
			    flags & CHAN_PMEAS_READY ? 1 : 0;
			//hal_port_check_lock(p->name);
			//p->locked =
		}

		break;

		/* Locking state (entered on calling hal_port_start_lock()). */
	case HAL_PORT_STATE_LOCKING:

		/* Once the locking FSM is done, go back to the "UP" state. */

		p->locked = hal_port_check_lock(p->name);

		if (p->locked) {
			pr_info("[main-fsm] Port %s locked.\n",
			      p->name);
			p->state = HAL_PORT_STATE_UP;
		}

		break;

		/* Calibration state (entered by starting the
		 * calibration with halexp_calibration_cmd()) */
	case HAL_PORT_STATE_CALIBRATION:

		/* Calibration still pending - if not anymore, go back
		 * to the "UP" state */
		if (p->rx_cal_pending || p->tx_cal_pending) {
		}		//calibration_fsm(p);
		else
			p->state = HAL_PORT_STATE_UP;

		break;
	}
}

static void hal_port_insert_sfp(struct hal_port_state * p)
{
	struct shw_sfp_header shdr;
	struct shw_sfp_caldata *cdata;
	char subname[48];
	int err;

	memset(&shdr, 0, sizeof(struct shw_sfp_header));
	memset(&p->calib.sfp_dom_raw, 0, sizeof(struct shw_sfp_dom));
	err = shw_sfp_read_verify_header(p->hw_index, &shdr);
	memcpy(&p->calib.sfp_header_raw, &shdr, sizeof(struct shw_sfp_header));
	if (err == -2) {
		pr_error("%s SFP module not inserted. Failed to read SFP "
			 "configuration header\n", p->name);
		return;
	} else if (err < 0) {
		pr_error("Failed to read SFP configuration header for %s\n",
			 p->name);
		return;
	}
	if (hal_shmem->read_sfp_diag == READ_SFP_DIAG_ENABLE
	    && shdr.diagnostic_monitoring_type & SFP_DIAGNOSTIC_IMPLEMENTED) {
		pr_info("SFP Diagnostic Monitoring implemented in SFP plugged"
			" to port %d (%s)\n", p->hw_index + 1, p->name);
		if (shdr.diagnostic_monitoring_type & SFP_ADDR_CHANGE_REQ) {
			pr_warning("SFP in port %d (%s) requires special "
				   "address change before accessing Diagnostic"
				   " Monitoring, which is not implemented "
				   "right now\n", p->hw_index + 1, p->name);
		} else {
			/* copy coontent of SFP's Diagnostic Monitoring */
			shw_sfp_read_dom(p->hw_index, &p->calib.sfp_dom_raw);
			if (err < 0) {
				pr_error("Failed to read SFP Diagnostic "
					 "Monitoring for port %d (%s)\n",
					 p->hw_index + 1, p->name);
			}
			p->has_sfp_diag = 1;
		}

	}
	pr_info("SFP Info: Manufacturer: %.16s P/N: %.16s, S/N: %.16s\n",
	      shdr.vendor_name, shdr.vendor_pn, shdr.vendor_serial);
	cdata = shw_sfp_get_cal_data(p->hw_index, &shdr);
	if (cdata) {
		/* Alpha is not known now. It is read later from the fibers'
		 * database. */
		pr_info("%s SFP Info: (%s) delta Tx %d, delta Rx %d, "
			"TX wl: %dnm, RX wl: %dnm\n", p->name,
			cdata->flags & SFP_FLAG_CLASS_DATA
			? "class-specific" : "device-specific",
			cdata->delta_tx_ps, cdata->delta_rx_ps, cdata->tx_wl,
			cdata->rx_wl);

		memcpy(&p->calib.sfp, cdata,
		       sizeof(struct shw_sfp_caldata));
		/* Mark SFP as found in data base */
		p->calib.sfp.flags |= SFP_FLAG_IN_DB;
	} else {
		pr_error("Unknown SFP vn=\"%.16s\" pn=\"%.16s\" "
			"vs=\"%.16s\" on port %s\n", shdr.vendor_name,
			shdr.vendor_pn, shdr.vendor_serial, p->name);
		memset(&p->calib.sfp, 0, sizeof(p->calib.sfp));
	}

	p->state = HAL_PORT_STATE_LINK_DOWN;
	shw_sfp_set_tx_disable(p->hw_index, 0);
	/* Copy the strings anyways, for informative value in shmem */
	strncpy(p->calib.sfp.vendor_name, (void *)shdr.vendor_name, 16);
	strncpy(p->calib.sfp.part_num, (void *)shdr.vendor_pn, 16);
	strncpy(p->calib.sfp.vendor_serial, (void *)shdr.vendor_serial, 16);
	/* check if SFP is 1GbE */
	p->calib.sfp.flags |= shdr.br_nom == SFP_SPEED_1Gb ? SFP_FLAG_1GbE : 0;
	p->calib.sfp.flags |= shdr.br_nom == SFP_SPEED_1Gb_10 ? SFP_FLAG_1GbE : 0;

	/*
	 * Now, we should fix the alpha value according to fiber
	 * type. Alpha does not depend on the SFP, but on the
	 * speed ratio of the SFP frequencies over the specific
	 * fiber. Thus, rely on the fiber type for this port.
	 */
	sprintf(subname, "alpha_%i_%i", p->calib.sfp.tx_wl, p->calib.sfp.rx_wl);
	err = libwr_cfg_convert2("FIBER%02i_PARAMS", subname,
				 LIBWR_DOUBLE, &p->calib.sfp.alpha,
				 p->fiber_index);
	if (!err) {
		/* Now we know alpha, so print it. */
		pr_info("%s SFP Info: alpha %.3f (* 1e6) found for TX wl: %dnm,"
			" RX wl: %dmn\n", p->name, p->calib.sfp.alpha * 1e6,
			p->calib.sfp.tx_wl, p->calib.sfp.rx_wl);
		return;
	}

	/* Try again, with the opposite direction (rx/tx) */
	sprintf(subname, "alpha_%i_%i", p->calib.sfp.rx_wl, p->calib.sfp.tx_wl);
	err = libwr_cfg_convert2("FIBER%02i_PARAMS", subname,
				 LIBWR_DOUBLE, &p->calib.sfp.alpha,
				 p->fiber_index);
	if (!err) {
		p->calib.sfp.alpha = (1.0 / (1.0 + p->calib.sfp.alpha)) - 1.0;
		/* Now we know alpha, so print it. */
		pr_info("%s SFP Info: alpha %.3f (* 1e6) found for TX wl: %dnm,"
			" RX wl: %dmn\n", p->name, p->calib.sfp.alpha * 1e6,
			p->calib.sfp.tx_wl, p->calib.sfp.rx_wl);
		return;
	}

	pr_error("Port %s, SFP vn=\"%.16s\" pn=\"%.16s\" vs=\"%.16s\", "
		"fiber %i: no alpha known\n", p->name,
		p->calib.sfp.vendor_name, p->calib.sfp.part_num,
		p->calib.sfp.vendor_serial, p->fiber_index);
	p->calib.sfp.alpha = 0;
}

static void hal_port_remove_sfp(struct hal_port_state * p)
{
	hal_port_link_down(p, 0);
	p->state = HAL_PORT_STATE_DISABLED;
	/* clean SFP's details when removing SFP */
	memset(&p->calib.sfp, 0, sizeof(p->calib.sfp));
	memset(&p->calib.sfp_header_raw, 0, sizeof(struct shw_sfp_header));
	memset(&p->calib.sfp_dom_raw, 0, sizeof(struct shw_sfp_dom));
	p->has_sfp_diag = 0;
}

/* detects insertion/removal of SFP transceivers */
static void hal_port_poll_sfp(void)
{
	if (libwr_tmo_expired(&hal_port_tmo_sfp)) {
		uint32_t mask = shw_sfp_module_scan();
		static int old_mask = 0;

		if (mask != old_mask) {
			int i, hw_index;
			for (i = 0; i < HAL_MAX_PORTS; i++) {
				hw_index = ports[i].hw_index;

				if (ports[i].in_use
				    && (mask ^ old_mask) & (1 << hw_index)) {
					int insert = mask & (1 << hw_index);
					pr_info("SFP Info: Detected SFP %s "
					      "on port %s.\n",
					      insert ? "insertion" : "removal",
					      ports[i].name);
					if (insert)
						hal_port_insert_sfp(&ports[i]);
					else
						hal_port_remove_sfp(&ports[i]);
				}
			}
		}
		old_mask = mask;
	}
}

/* Executes the port FSM for all ports. Called regularly by the main loop. */
void hal_port_update_all()
{
	int i;

	/* poll_rts_state does not write to shmem */
	poll_rts_state();

	/* lock shmem */
	wrs_shm_write(hal_shmem_hdr, WRS_SHM_WRITE_BEGIN);
	hal_port_poll_sfp();

	for (i = 0; i < HAL_MAX_PORTS; i++)
		if (ports[i].in_use)
			hal_port_fsm(&ports[i]);

	if (hal_shmem->read_sfp_diag == READ_SFP_DIAG_ENABLE
	    && libwr_tmo_expired(&update_sfp_dom_tmo)) {
		for (i = 0; i < HAL_MAX_PORTS; i++) {
			/* update DOM only for plugged ports with DOM
			 * capabilities */
			if (ports[i].in_use
			    && ports[i].state != HAL_PORT_STATE_DISABLED
			    && (ports[i].has_sfp_diag)) {
				shw_sfp_update_dom(ports[i].hw_index,
						  &ports[i].calib.sfp_dom_raw);
			}
		}
	}

	/* unlock shmem */
	wrs_shm_write(hal_shmem_hdr, WRS_SHM_WRITE_END);

	/* try to open ppsi's shmem */
	if (!try_open_ppsi_shmem())
		return;

	if (libwr_tmo_expired(&update_link_leds_tmo)) {
		/* update color of the link LEDs */
		update_link_leds();
	}

	if (libwr_tmo_expired(&update_sync_leds_tmo)) {
		/* update LEDs of synced ports */
		update_sync_leds();
	}
}

int hal_port_enable_tracking(const char *port_name)
{
	const struct hal_port_state *p = hal_lookup_port(ports,
						  hal_port_nports, port_name);

	if (!p)
		return -1;

	return rts_enable_ptracker(p->hw_index, 1); /* 0 or -1 already */
}

/* Triggers the locking state machine, called by the PTPd during the
 * WR link setup phase. */
int hal_port_start_lock(const char *port_name, int priority)
{
	struct hal_port_state *p = hal_lookup_port(ports, hal_port_nports,
						   port_name);

	if (!p)
		return -1;

	/* can't lock to a disconnected port */
	if (p->state != HAL_PORT_STATE_UP)
		return -1;

	/* lock shmem */
	wrs_shm_write(hal_shmem_hdr, WRS_SHM_WRITE_BEGIN);
	/* fixme: check the main FSM state before */
	p->state = HAL_PORT_STATE_LOCKING;
	wrs_shm_write(hal_shmem_hdr, WRS_SHM_WRITE_END);

	pr_info("Locking to port: %s\n", port_name);

	rts_set_mode(RTS_MODE_BC);

	return rts_lock_channel(p->hw_index, 0); /* 0 or -1 already */
}

/* Returns 1 if the port is locked */
int hal_port_check_lock(const char *port_name)
{
	const struct hal_port_state *p = hal_lookup_port(ports,
						hal_port_nports, port_name);
	struct rts_pll_state *hs = &hal_port_rts_state;

	if (!p)
		return 0; /* was -1, but it would confuse the caller */

	if (!hal_port_rts_state_valid)
		return 0;

	if (hs->delock_count > 0)
		return 0;

	return (hs->current_ref == p->hw_index &&
		(hs->flags & RTS_DMTD_LOCKED) &&
		(hs->flags & RTS_REF_LOCKED));
}

int hal_port_reset(const char *port_name)
{
	struct hal_port_state *p = hal_lookup_port(ports,
						  hal_port_nports, port_name);

	if (!p)
		return -1;

	if (p->state != HAL_PORT_STATE_LINK_DOWN
	    && p->state != HAL_PORT_STATE_DISABLED) {
		if (p->locked) {
			pr_info("Switching RTS to use local reference\n");
			if (hal_get_timing_mode()
			    != HAL_TIMING_MODE_GRAND_MASTER)
				rts_set_mode(RTS_MODE_GM_FREERUNNING);
		}

		/* turn off synced LED */
		set_led_synced(p->hw_index, 0);

		/* turn off link/wrmode LEDs */
		set_led_wrmode(p->hw_index, SFP_LED_WRMODE_OFF);
		hal_port_reset_state(p);
		p->state = HAL_PORT_STATE_RESET;

		rts_enable_ptracker(p->hw_index, 0);
		pr_info("%s: link down\n", p->name);

		return 1;
	}
	return 0;
}

/* to avoid i2c transfers to set the link LEDs, cache their state */
static void set_led_wrmode(int p_index, int val)
{
	/* We assume that after the HAL is started all LEDs are off */
	static int leds_map[HAL_MAX_PORTS];

	if (p_index >= HAL_MAX_PORTS)
		return;

	if (leds_map[p_index] == val) {
		/* value has not changed */
		return;
	}

	/* update the LED, don't forget to turn off LEDs if needed */
	if (val == SFP_LED_WRMODE_SLAVE) {
		/* cannot set and clear LED in the same call! */
		shw_sfp_set_generic(p_index, 1, SFP_LED_WRMODE1);
		shw_sfp_set_generic(p_index, 0, SFP_LED_WRMODE2);
	} else if (val == SFP_LED_WRMODE_OTHER) {
		/* cannot set and clear LED in the same call! */
		shw_sfp_set_generic(p_index, 0, SFP_LED_WRMODE1);
		shw_sfp_set_generic(p_index, 1, SFP_LED_WRMODE2);
	} else if (val == SFP_LED_WRMODE_MASTER) {
		shw_sfp_set_generic(p_index, 1,
				    SFP_LED_WRMODE1 | SFP_LED_WRMODE2);
	} else if (val == SFP_LED_WRMODE_OFF) {
		shw_sfp_set_generic(p_index, 0,
				    SFP_LED_WRMODE1 | SFP_LED_WRMODE2);
	}
	leds_map[p_index] = val;
}

static int read_ppsi_instances(void){
	unsigned ii;
	unsigned retries = 0;

	/* read data, with the sequential lock to have all data consistent */
	while (1) {
		ii = wrs_shm_seqbegin(ppsi_head);
		memcpy(&ppsi_instances_local, ppsi_instances,
		       ppsi_nlinks * sizeof(*ppsi_instances));
		retries++;
		if (retries > 100)
			return -1;
		if (!wrs_shm_seqretry(ppsi_head, ii))
			break; /* consistent read */
	}

	return 0;
}

static void update_link_leds(void)
{
	int i;
	int j;
	int port_state;

	/* read servo */
	if (read_ppsi_instances())
		return;

	for (i = 0; i < HAL_MAX_PORTS; i++) {
		if (!ports[i].in_use
		    || !state_up(ports[i].state)) {
			/* skip ports not in use nor up */
			continue;
		}

		port_state = 0;
		for (j = 0; j < ppsi_nlinks; j++) {
			/* There might be multiple ppsi instances on
			 * a particular port */
			if (strcmp(ports[i].name,
					ppsi_instances[j].cfg.iface_name)) {
				/* Instance not for this interface
				  * skip */
				continue;
			}
			/* Pick the most important state */
			if (ppsi_instances[j].state == PPS_SLAVE) {
				/* Slave found, not possible to find more
				 * important state than this, break */
				port_state = PPS_SLAVE;
				break;
			}

			if (ppsi_instances[j].state == PPS_MASTER) {
				port_state = PPS_MASTER;
				/* Don't brake, keep trying to find slave */
				continue;
			}
		}
		if (port_state == PPS_SLAVE)
			set_led_wrmode(i, SFP_LED_WRMODE_SLAVE);
		else if (port_state == PPS_MASTER)
			set_led_wrmode(i, SFP_LED_WRMODE_MASTER);
		else
			set_led_wrmode(i, SFP_LED_WRMODE_OTHER);
	}
}


/* to avoid i2c transfers to set the synced LEDs, cache their state */
static void set_led_synced(int p_index, int val)
{
	/* We assume that after the HAL is started all LEDs are off */
	static int leds_map[HAL_MAX_PORTS];

	if (p_index >= HAL_MAX_PORTS)
		return;

	if (leds_map[p_index] == val) {
		/* value has not changed */
		return;
	}

	/* update the LED */
	shw_sfp_set_led_synced(p_index, val);
	leds_map[p_index] = val;
}

static void update_sync_leds(void)
{
	int i;
	static uint32_t update_count = 0;
	static uint32_t since_last_servo_update = 0;

	/* read servo */
	if (read_servo())
		return;

	if (!strnlen(ppsi_servo_local.if_name, 16))
		return;

	for (i = 0; i < HAL_MAX_PORTS; i++) {
		/* Check:
		 * --port in use
		 * --link is up
		 * --interface name matches between port and servo
		 * If all is true then turn on the sync status LED, otherwise
		 * turn it off
		 */
		if (ports[i].in_use
		    && state_up(ports[i].state)
		    && !strcmp(ppsi_servo_local.if_name, ports[i].name)) {
			if (update_count == ppsi_servo_local.update_count) {
				if (since_last_servo_update < 7)
					since_last_servo_update++;
			} else {
				since_last_servo_update = 0;
				update_count = ppsi_servo_local.update_count;
			}
			/* Check:
			* --port in slave mode
			* --servo is in track phase
			* --servo is updating
			*/
			if (ports[i].mode == HEXP_PORT_MODE_WR_SLAVE
			    && ppsi_servo_local.state == WR_TRACK_PHASE
			    && since_last_servo_update < 7
			    ) {
				set_led_synced(i, 1);
			} else {
				set_led_synced(i, 0);
			}
		}
	}
}

static int read_servo(void){
	unsigned ii;
	unsigned retries = 0;

	/* read data, with the sequential lock to have all data consistent */
	while (1) {
		ii = wrs_shm_seqbegin(ppsi_head);
		memcpy(&ppsi_servo_local, ppsi_servo, sizeof(*ppsi_servo));
		retries++;
		if (retries > 100)
			return -1;
		if (!wrs_shm_seqretry(ppsi_head, ii))
			break; /* consistent read */
	}

	return 0;
}

static int try_open_ppsi_shmem(void)
{
	int ret;
	struct pp_globals *ppg;
	static int open_error;

	if (ppsi_servo && ppsi_instances) {
		/* shmem already opened */
		return 1;
	}

	if (!ppsi_head) {
		ret = wrs_shm_get_and_check(wrs_shm_ptp, &ppsi_head);
		if (ret == WRS_SHM_OPEN_FAILED) {
			if (open_error > 100)
				pr_error("Unable to open PPSI's shm !\n");
			else
				open_error++;
			return 0;
		}
		if (ret == WRS_SHM_WRONG_VERSION) {
			pr_error("Unable to read PPSI's version!\n");
			return 0;
		}
		if (ret == WRS_SHM_INCONSISTENT_DATA) {
			pr_error("Unable to read consistent data from PPSI's "
				 "shmem!\n");
			return 0;
		}
	}

	/* check ppsi's shm version */
	if (ppsi_head->version != WRS_PPSI_SHMEM_VERSION) {
		pr_error("Unknown PPSI's shm version %i (known is %i)\n",
			 ppsi_head->version, WRS_PPSI_SHMEM_VERSION);
		return 0;
	}
	ppg = (void *)ppsi_head + ppsi_head->data_off;

	/* there is an assumption that there is only one servo in ppsi! */
	ppsi_servo = wrs_shm_follow(ppsi_head, ppg->global_ext_data);
	if (!ppsi_servo) {
		pr_error("Cannot follow ppsi_servo in shmem.\n");
		return 0;
	}

	ppsi_instances = wrs_shm_follow(ppsi_head, ppg->pp_instances);
	if (!ppsi_instances) {
		pr_error("Cannot follow pp_instances in shmem.\n");
		return 0;
	}

	ppsi_nlinks = ppg->nlinks;

	return 1;
}

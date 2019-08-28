/*
 * Copyright (C) 2019 CERN (www.cern.ch)
 * Author: Jean-Claude BAU - CERN
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 *
 *
 */

#include <rt_ipc.h>

#include <libwr/hal_shmem.h>
#include <libwr/config-lpcalib.h>

#include "hal_exports.h"
#include "hal_port_gen_fsm.h"
#include "driver_stuff.h"
#include "hal_port_leds.h"
#include "hal_main.h"
#include "hal_ports.h"
#include "hal_port_fsm_txP.h"

/**
 * State machine
 * States :
 *    - HAL_PORT_TX_SETUP_STATE_START:
 *    	Inital state
 *    - .....
 *    - HAL_PORT_TX_SETUP_STATE_DONE:
 *    	TX setup terminated
 * Events :
 *    - timer      : triggered regularly to execute background work
 */

/* external prototypes */
static  int _buildEvents(void * vpfg);
static int _hal_port_tx_setup_state_start(void *vpfg, int eventMsk, int isNewState);
static int _hal_port_tx_setup_state_validate(void *vpfg, int eventMsk, int isNewState);
static int _hal_port_tx_setup_state_reset_pcs(void *vpfg, int eventMsk, int isNewState);
static int _hal_port_tx_setup_state_wait_lock(void *vpfg, int eventMsk, int isNewState);
static int _hal_port_tx_setup_state_measure_phase(void *vpfg, int eventMsk, int isNewState);
static int _hal_port_tx_setup_state_done(void *vpfg, int eventMsk, int isNewState);

static halPortStateTable_t _fsmStateTable[] =
{
		{ .state=HAL_PORT_TX_SETUP_STATE_START,
				.stateName="START",
				FSM_SET_FCT_NAME(_hal_port_tx_setup_state_start)
		},
		{ .state=HAL_PORT_TX_SETUP_STATE_RESET_PCS,
				.stateName="RESET_PCS",
				FSM_SET_FCT_NAME(_hal_port_tx_setup_state_reset_pcs)
		},
		{ .state=HAL_PORT_TX_SETUP_STATE_MEASURE_PHASE,
				.stateName="MEASURE_PHASE",
				FSM_SET_FCT_NAME(_hal_port_tx_setup_state_measure_phase)
		},
		{ .state=HAL_PORT_TX_SETUP_STATE_WAIT_LOCK,
				.stateName="WAIT_LOCK",
				FSM_SET_FCT_NAME(_hal_port_tx_setup_state_wait_lock)
		},
		{ .state=HAL_PORT_TX_SETUP_STATE_VALIDATE,
				.stateName="VALIDATE",
				FSM_SET_FCT_NAME(_hal_port_tx_setup_state_validate)
		},
		{ .state=HAL_PORT_TX_SETUP_STATE_DONE,
				.stateName="DONE",
				FSM_SET_FCT_NAME(_hal_port_tx_setup_state_done)
		},
		{.state=1}
};

static halPortEventTable_t _fsmEvtTable[] = {
		{
				.evtMask = HAL_PORT_TX_SETUP_EVENT_TIMER,
				.evtName="TIMER"
		},
		{ .evtMask = -1 } };

static halPortFsmGen_t _portFsm = {
		.fsm_name="PortFsmTxSetup",
		.fctBuilEvents=_buildEvents,
		.pt=_fsmStateTable,
		.pe=_fsmEvtTable
};

//TODO-ML: the below structure is redundant, consider reogranization to use
//         hal_port_rts_state
static struct rts_pll_state _pll_state;
/* path to the file where Low Phase Drift calib parameters are stored */
static char *_calibrationFileName = "/update/tx_phase_cal.conf";
struct config_file *_calibrationConfig; // Calibration config form file

static __inline__ void updatePllState(struct hal_port_state * ps) {
	struct halGlobalLPDC * gl = ps->lpdc.globalLpdc;
	if (ps->hw_index == gl->firstLpdcPort)
	{
		// update PLL state once for all ports
		rts_get_state(&_pll_state);
	}
}

static __inline__ void txSetupDone(struct hal_port_state * ps) {
	struct halGlobalLPDC * gl = ps->lpdc.globalLpdc;
	gl->numberOfTxSetupDonePorts++;
}

static __inline__ int txSetupDoneOnAllPorts(struct hal_port_state * ps) {
	struct halGlobalLPDC * gl = ps->lpdc.globalLpdc;
	return gl->numberOfTxSetupDonePorts == gl->numberOfLpdcPorts;
}

/* prototypes */
static void _write_tx_calibration_file(struct hal_port_state * _ps);
static void _load_tx_calibration_file(struct hal_port_state * ps);
static int _within_range(int x, int minval, int maxval, int wrap);
/*
 * START state
 *
 * If entering in state then
 *      Set LPDC supported accordingly to the hardware
 * fi
 * if  LPDC is not supported then state=DONE
 *
 */
static int _hal_port_tx_setup_state_start(void *vpfg, int eventMsk, int isNewState) {
	struct hal_port_state * ps=((halPortFsmGen_t *)vpfg)->ps;

	if ( !ps->lpdc.isSupported ) {
		// NO LPDC support
		_fireState(vpfg,HAL_PORT_TX_SETUP_STATE_DONE);
		return 0;
	} else {
		// LPDC support
		halPortLpdcTx_t *txSetup=ps->lpdc.txSetup;
		struct halGlobalLPDC * gl = ps->lpdc.globalLpdc;

		txSetup->attempts=0;
		txSetup->expected_phase = 0;
		txSetup->expected_phase_valid = 0;
		txSetup->tollerance = 300;
		txSetup->update_cnt = 0;

		_pll_state.channels[ps->hw_index].flags = 0;

		rts_enable_ptracker(ps->hw_index, 0);

		pcs_writel(ps, MDIO_LPC_CTRL_RESET_RX |
			      MDIO_LPC_CTRL_DMTD_SOURCE_TXOUTCLK,
			      MDIO_LPC_CTRL);

		led_set_wrmode(ps->hw_index,SFP_LED_WRMODE_TX_CALIB);
		_fireState(vpfg,HAL_PORT_TX_SETUP_STATE_RESET_PCS);
	}
	return 0;
}

/*
 * RESET_PCS state
 *
 *
 */
static int _hal_port_tx_setup_state_reset_pcs(void *vpfg, int eventMsk, int isNewState) {
	struct hal_port_state * ps=((halPortFsmGen_t *)vpfg)->ps;

	rts_enable_ptracker(ps->hw_index, 0);
	pcs_writel(ps, MDIO_LPC_CTRL_RESET_TX |
		      MDIO_LPC_CTRL_RESET_RX |
		      MDIO_LPC_CTRL_DMTD_SOURCE_TXOUTCLK,
		      MDIO_LPC_CTRL);
	shw_udelay(1);
	pcs_writel(ps, MDIO_LPC_CTRL_RESET_RX |
		      MDIO_LPC_CTRL_DMTD_SOURCE_TXOUTCLK,
		      MDIO_LPC_CTRL);

	_fireState(vpfg,HAL_PORT_TX_SETUP_STATE_WAIT_LOCK);
	return 0;
}

/*
 * WAIT_LOCK state
 *
 *
 */
static int _hal_port_tx_setup_state_wait_lock(void *vpfg, int eventMsk, int isNewState) {
	struct hal_port_state * ps=((halPortFsmGen_t *)vpfg)->ps;
	uint32_t value;

	if ( pcs_readl(ps, MDIO_LPC_STAT,&value)>=0 ) {
		if ( (value & MDIO_LPC_STAT_RESET_TX_DONE)!=0 ) {
			ps->lpdc.txSetup->attempts++;
			rts_enable_ptracker(ps->hw_index, 1);
			_pll_state.channels[ps->hw_index].flags = 0;

			_fireState(vpfg,HAL_PORT_TX_SETUP_STATE_MEASURE_PHASE);
		}
	}
	return 0;
}

/*
 * MEASURE_PHASE state
 *
 *
 */
static int _hal_port_tx_setup_state_measure_phase(void *vpfg, int eventMsk, int isNewState) {
	struct hal_port_state * ps = ((halPortFsmGen_t *) vpfg)->ps;
	halPortLpdcTx_t * txSetup;

	updatePllState(ps);
	if (!(_pll_state.channels[ps->hw_index].flags & CHAN_PMEAS_READY))
		return 0; // keep waiting

	txSetup = ps->lpdc.txSetup;
	int phase = _pll_state.channels[ps->hw_index].phase_loopback;
	txSetup->measured_phase = phase;

	if (!txSetup->expected_phase_valid) {
		if (txSetup->cal_saved_phase_valid) {
			pr_info("Using phase from file :%d\n", txSetup->cal_saved_phase);
			txSetup->expected_phase = txSetup->cal_saved_phase;
		} else {
			int phi = phase;

			do // find the phase bin right after the rising parallel clock edge
			{
				txSetup->expected_phase = phi;
				phi -= 800;
			} while (phi > 0);
		}
		txSetup->expected_phase_valid = 1;
	}

	int phase_min = txSetup->expected_phase - txSetup->tollerance;
	int phase_max = txSetup->expected_phase + txSetup->tollerance;

	//TODO-ML: change to name of interface, remove two phases
	pr_info("TX Calibration: upd port %d phase %d %d after %d "
			"attempts target %d tollerance %d (temp = %.3f degC)\n",
			ps->hw_index, txSetup->measured_phase, txSetup->measured_phase,
			txSetup->attempts, txSetup->expected_phase, txSetup->tollerance,
			hal_get_fpga_temperature() / 256.0);

	if (_within_range(phase, phase_min, phase_max, 16000)) {
		int i;

		pr_info("FIX port %d phase %d after %d attempts "
				"(temp = %.3f degC)\n", ps->hw_index, txSetup->measured_phase,
				txSetup->attempts, hal_get_fpga_temperature() / 256.0);
		rts_enable_ptracker(ps->hw_index, 0);
		rts_enable_ptracker(ps->hw_index, 1);

		for (i = 0; i < RTS_PLL_CHANNELS; i++)
			_pll_state.channels[i].flags = 0;

		_fireState(vpfg, HAL_PORT_TX_SETUP_STATE_VALIDATE);
	} else
		_fireState(vpfg, HAL_PORT_TX_SETUP_STATE_RESET_PCS);

	return 0;
}

/*
 * Validate state
 *
 *
 */
static int _hal_port_tx_setup_state_validate(void *vpfg, int eventMsk, int isNewState) {
	struct hal_port_state * ps = ((halPortFsmGen_t *) vpfg)->ps;
	halPortLpdcTx_t *txSetup;

	updatePllState(ps);
	if (!(_pll_state.channels[ps->hw_index].flags & CHAN_PMEAS_READY))
		return 0; // keep waiting

	txSetup = ps->lpdc.txSetup;
	txSetup->measured_phase = _pll_state.channels[ps->hw_index].phase_loopback;
	pr_info("Port %d: TX calibration complete\n", ps->hw_index + 1);
	rts_enable_ptracker(ps->hw_index, 0);

	// enable the PCS on the port
	pcs_writel(ps, MDIO_LPC_CTRL_RESET_RX |
		      MDIO_LPC_CTRL_TX_ENABLE |
		      MDIO_LPC_CTRL_DMTD_SOURCE_RXRECCLK,
		      MDIO_LPC_CTRL);

	led_set_wrmode(ps->hw_index,SFP_LED_WRMODE_OFF);

	_fireState(vpfg,HAL_PORT_TX_SETUP_STATE_DONE);
	txSetupDone(ps);
	if(txSetupDoneOnAllPorts(ps))
		_write_tx_calibration_file(ps);

	return 0;
}

/*
 * DONE state
 *
 * Return final state machine reached
 */
static int _hal_port_tx_setup_state_done(void *vpfg, int eventMsk, int isNewState) {
	return 1; /* Final state reached */
}

/* Build events mask */
static  int _buildEvents(void *vpfg) {
	return HAL_PORT_TX_SETUP_EVENT_TIMER;
}

/* Initialize tx_setup-this is a global init, executed once for all ports/FSMs.
   It includes:
   - loading calibration file if exists
   - initializing a global structure and filling it in - this structure is
     needed by tx calibration only (so far)
*/
void hal_port_tx_setup_init(struct hal_port_state * ps,  struct halGlobalLPDC *globalLpdc) {
	int index;
	struct hal_port_state * _ps = ps;
	int numberOfLpdcPorts = 0;
	int firstLpdcPort = -1;
	int lastLpdcPort = -1;

	/* check whether there is any port that supports LPDC,
	   if there is such port, load the tx calibration file.*/
	for (index = 0; index < HAL_MAX_PORTS; index++){
		if(_ps->in_use && _ps->lpdc.isSupported){

			/* if this is the first port with LPDC support, 
			   allocate memory for the global struct */
			if( !globalLpdc )
				globalLpdc = malloc(sizeof(struct halGlobalLPDC));

			/* link the global structure from each port*/
			_ps->lpdc.globalLpdc = globalLpdc;
			
			/* if this ist he first supporetd port, save its index*/
			if (firstLpdcPort < 0)
				firstLpdcPort = index;
			
			/*remember the index, just in case it is the last LPDC port*/
			lastLpdcPort = index;
			
			/* count number of supported ports*/
			numberOfLpdcPorts++;
		}
		_ps++;
	}

	/* if there are any LPDC ports, do some preparation */
	if(globalLpdc && numberOfLpdcPorts) {

		/* fill in the global structure */
		globalLpdc->numberOfLpdcPorts = numberOfLpdcPorts;
		globalLpdc->numberOfTxSetupDonePorts = 0;
		globalLpdc->calFileSynced = 0;
		globalLpdc->firstLpdcPort = firstLpdcPort;
		globalLpdc->lastLpdcPort = lastLpdcPort;
		pr_info("WR switch supports LPDC on %d ports ("
		        "first port is %d, last port is %d)\n",
		        globalLpdc->numberOfLpdcPorts,
		        globalLpdc->firstLpdcPort,
		        globalLpdc->lastLpdcPort);

		/* load the calib file*/
		_load_tx_calibration_file(ps);
        }
}

/* Init the TX SETUP FSM on a given port */
void hal_port_tx_setup_init_fsm(struct hal_port_state * ps ) {
	_portFsm.ps=ps;
	_portFsm.st=&ps->lpdc.txSetupStates;
	ps->lpdc.txSetupStates.state=-1;
	_fireState(&_portFsm,HAL_PORT_TX_SETUP_STATE_START);
}

/* FSM state machine for TX setup on a given port
 * Returned value:
 *  1: when final state has been reached
 *  0: when final state has not been reached
 *  -1: error detected
 */

int  hal_port_tx_setup_state_fsm( struct hal_port_state * ps ) {
	_portFsm.ps=ps;
	_portFsm.st=&ps->lpdc.txSetupStates;
	return hal_port_generic_fsm(&_portFsm);
}
/* if config is present then update the calibration data */
static void _load_tx_calibration_file(struct hal_port_state * ps) {

	int i = 0;
	struct halGlobalLPDC * gl = ps->lpdc.globalLpdc;

	// Read calibration file, if it exists
	_calibrationConfig = cfg_load(_calibrationFileName, 0);


	if ( !_calibrationConfig ) {
		pr_info("Can't load TX phase calibration data file: %s\n",
			_calibrationFileName);
		gl->calFileSynced = 0;
		return;
        }

	pr_info("Loading LPCD config data from %s\n", _calibrationFileName);

	for (i = 0; i < HAL_MAX_PORTS; i++){
		if (ps->in_use && ps->lpdc.isSupported)
		{
			char key_name[80];
			int value;
			snprintf(key_name, sizeof(key_name), "TX_PHASE_PORT%d", 
				ps->hw_index);

			if(cfg_get_int( _calibrationConfig, key_name, &value) )
			{
				//TODO-ML: change to name
				pr_info("cal: %d %d\n", ps->hw_index, value);
				ps->lpdc.txSetup->cal_saved_phase = value;
				ps->lpdc.txSetup->cal_saved_phase_valid = 1;
			}
		}
		ps++;
	}
	gl->calFileSynced = 1;
	cfg_close(_calibrationConfig);
}

static int file_exists(const char *filename)
{
	FILE *f = fopen(filename, "r");

	if (f != NULL)
	{
		fclose(f);
		return 1;
	}

	return 0;
}

static void _write_tx_calibration_file(struct hal_port_state * _ps)
{
	int i;
	struct hal_port_state * ps=_ps;
	struct halGlobalLPDC * gl = ps->lpdc.globalLpdc;

	if(gl->calFileSynced)
		return;

	if (file_exists(_calibrationFileName))
	{
		pr_warning("Tx calibration file exists, yet it has not been"
		    " synched. Something seems wrong. Should not get here\n");
		return;
	}

	struct config_file *cfg = cfg_load(_calibrationFileName, 1);

	ps=_ps;
	for (i = 0; i < HAL_MAX_PORTS; i++) {
		if (ps->in_use && ps->lpdc.isSupported)
		{
			char key_name[80];
			snprintf(key_name, sizeof(key_name), "TX_PHASE_PORT%d", ps->hw_index);
			cfg_set_int(cfg, key_name, ps->lpdc.txSetup->measured_phase);
		}
		ps++;
	}

	pr_info( "Writing TX phase calibration data to %s\n", _calibrationFileName );
	cfg_save( cfg, _calibrationFileName );
	cfg_close(cfg);

	gl->calFileSynced = 1;
}

//min -269 max 331 x 9631 Rv 1
static int _within_range(int x, int minval, int maxval, int wrap)
{
	int rv;

	printf("min %d max %d x %d \n", minval, maxval, x);

	while (maxval >= wrap)
		maxval -= wrap;

	while (maxval < 0)
		maxval += wrap;

	while (minval >= wrap)
		minval -= wrap;

	while (minval < 0)
		minval += wrap;

	while (x < 0)
		x += wrap;

	while (x >= wrap)
		x -= wrap;

	if (maxval > minval)
		rv = (x >= minval && x <= maxval) ? 1 : 0;
	else
		rv = (x >= minval || x <= maxval) ? 1 : 0;

	return rv;
}



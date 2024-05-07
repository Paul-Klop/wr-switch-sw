# Author: Quentin GENOUD DUVILLARET (quentin.genoud@cern.ch)
# Scirpt to automate WRS calibration

import os
import sys
import signal
import time
import argparse
from scope import Scope
from wrs import WRS
from logger import Logger
from config import Config
from customthread import CustomThread
import threading
#import subprocess

#############
# FUNCTIONS #
#############
# Ctrl+C handler, quit immediately
def signal_handler(sig, frame):
    print("Ctrl+C captured, ending")
    quit()

def get_int_input(text, val_min, val_max):
    while True:
        value = int(input(text))
        if(value >= val_min and value <= val_max):
            break
    return value

def get_float_input(text, val_min, val_max):
    # First loop to check that the value is between min and max
    while True:
        # Second loop, to request a valid float
        while True:
            user_input = input(text)
            try:
                # Check if the input ends with 'ns' or 'ps'
                if user_input.endswith('ns'):
                    number = float(user_input[:-2])
                    # Convert ns to s
                    number = number / 1e9
                    break
                elif user_input.endswith('ps'):
                    number = float(user_input[:-2])
                    # Convert ps to s
                    number = number / 1e12
                    break
                else:
                    # Try to convert the input directly to a float
                    number = float(user_input)
                    break
            except ValueError:
                print("Invalid input. Please enter a valid float number with or without units 'ns' or 'ps'.")

        if(number >= val_min and number <= val_max):
            return number
        else:
            print("Invalid input. Please enter a valid float number in the interval [{} ; {}]".format(val_min, val_max))

def get_addr(text):
    while True:
        addr = input(text)
        response = os.system("ping -W 1 -c 1 " + addr + " > /dev/null")
        if(response == 0):
            break
        else:
            print("ERROR: wrong address or device is offline")
    return addr

def get_yes_no(text):
    while True:
        value = input(text + " (y/n)")
        if(value == "y" or value == "Y"):
            return True
        elif(value == "n" or value == "N"):
            return False

# Function used to measure rawDelayMM and to log the values
def get_rawDelayMM_mean_log(logger, wrs, meas_nb):
    curr_meas_nb = 0
    delayMM_sum = 0.00
    lines = ""
    logger.write_rawDelayMM_header()
    start = time.time()
    while(curr_meas_nb < meas_nb):
        curr_meas_nb += 1
        tstart = time.time()
        delayMM = wrs.get_rawDelayMM(wrs.sfp_port)
        logger.write_rawDelayMM(curr_meas_nb, delayMM)
        tdiff = (time.time() - tstart)
        delayMM_sum += delayMM
        #print("Meas {}: {}".format(curr_meas_nb, delayMM))
        twait = 1 - tdiff
        #print("Duration: " + str(tdiff) + ", Time to wait: " + str(twait))
        if(twait > 0):
            #print("Duration: " + str(tdiff) + ", Time to wait: " + str(twait))
            time.sleep(twait)
        print(".", end="", flush=True)
    #print("duration get raw delay MM mean log: {}".format(time.time()-start))
    return (delayMM_sum / meas_nb)

# Use threads to apply config and restart hald at the same time on GM & BC
def apply_config(gm, bc):
    stop = False

    def apply_config_gm(gm):
        gm.apply_config()

    def apply_config_bc(bc):
        bc.apply_config()

    def dots(id, stop):
        while(True):
            if(stop()):
                break
            print(".", end="", flush=True)
            time.sleep(1)

    t1 = threading.Thread(target=apply_config_gm, args=(gm,))
    t2 = threading.Thread(target=apply_config_bc, args=(bc,))
    t3 = threading.Thread(target=dots, args=(0, lambda: stop))
            
    t1.start()
    t2.start()
    t3.start()
            
    t1.join()
    t2.join()
    stop = True
    t3.join()
    # Replacing print() with a pr() function that always flushes (sys.stdout.flush())
    # may improve the precision of the shell output.

def set_wrs_config(gm, bc):
    # Set GM as GM (if it isn't), reset alpha and delays to 0
    print("INFO: configuring GM {}: ".format(gm.address), end="", flush=True)
    gm.set_mode_GM()
    for i in range(1, 19):
        gm.set_port_master(i)
    gm.set_alpha(0)
    gm.set_port_delays(gm.sfp_port, 0, 0)
    gm.restart_hald = True
    print("OK")

    print("INFO: configuring BC {}: ".format(bc.address), end="", flush=True)
    bc.set_mode_BC()
    bc.set_alpha(0)
    bc.set_port_slave(bc.sfp_port)
    bc.set_port_delays(bc.sfp_port, 0, 0)
    bc.restart_hald = True
    print("OK")

    print("INFO: applying config on GM & BC: ", end="", flush=True)
    apply_config(gm, bc)
    print("OK")

    print("INFO: checking WRSes configuration: ", end="", flush=True)
    gm_state = gm.is_gm();
    bc_state = bc.is_bc();
    if gm_state and bc_state:
        print("OK")
    else:
        print("FAIL: {} GM = {}, {} BC = {}".format(gm.address, gm_state, bc.address, bc_state))
        quit()

    print("INFO: waiting for GM PLL lock: ", end="", flush=True)
    gm.wait_until_locked(500)
    print("OK")


#############
# VARIABLES #
#############
## Default config ##
# Duration of each measure taken on the scope in s
meas_duration = 120
# Number of measures to be taken on the scope
meas_number = 20
# Number of measures of rawDelayMM to be averaged
delay_meas_nb = 20

## Default scope config ##
scope = Scope("MSO64B")
scope.model = "mso64b"
# horizontal config
scope.h_scale = 40e-12
scope.trigger_mode = "NORMAL"
scope.trigger_src = "CH1"
scope.trigger_slope = "RISE"
scope.trigger_level = 850e-3
# CH1 default config
scope.ch_label[0] = "PPS_GM"
scope.ch_vscale[0] = 10e-3
scope.ch_termination[0] = 50
scope.ch_bw[0] = "FULL"
scope.ch_offset[0] = 850e-3
# CH3 default config
scope.ch_label[2] = "PPS_BC"
scope.ch_vscale[2] = 10e-3
scope.ch_termination[2] = 50
scope.ch_bw[2] = "FULL"
scope.ch_offset[2] = 850e-3
# Measure1 config
scope.meas_type[0] = "DELAY"
scope.meas_src1[0] = "CH1"
scope.meas_src2[0] = "CH3"
scope.meas_riseh[0] = "1.6"
scope.meas_risem[0] = "0.85"
scope.meas_risel[0] = "0"
# Reference time base source
scope.ref_src = "EXTERNAL"

## WRSes used during the calibration ##
gm = WRS("calib_gm")
bc = WRS("calib_bc")

## Config parser ##
config = Config(scope, gm, bc)

## Logger ##
logger = Logger(scope, gm, bc)


#############
### START ###
#############
# Declare CTRL-C (SIGINT) signal handler
signal.signal(signal.SIGINT, signal_handler)

# Arg parser
parser = argparse.ArgumentParser(
                    prog=sys.argv[0],
                    description='Automate WRS calibration procedure',
                    epilog='')
"""
parser.add_argument('wrs_addr',
        help="Address of the White Rabbit Switch")
parser.add_argument('-p', '--port',
        required=True,
        type=int,
        choices=range(1, 19),
        help="The port which should be used to measure rawDelayMM")
parser.add_argument('-d', '--delta',
        action='store_true',
        required=False,
        help="If set, measure the short fiber delta1 value, else, user must provide it")
"""

parser.add_argument('-e', '--export',
        required=False,
        type=str,
        help="If set, export tx_phase_cal.conf from specified WRS to all others")
parser.add_argument('-t', '--t24p',
        required=False,
        type=int,
        choices=range(1, 19),
        help="Enable t24p measures and start at specified port")
parser.add_argument('-a', '--alpha',
        required=False,
        type=float,
        help="Set the value of alpha parameter on both switches then quit")
parser.add_argument('-l', '--log',
        required=False,
        type=str,
        #type=argparse.FileType('w'),
        default="measures.csv",
        help="Name of the file to store the measures. Default: measures.csv")
parser.add_argument('-n', '--number',
        required=False,
        type=int,
        help="Number of rawDelayMM measures to be taken")
parser.add_argument('-d', '--duration',
        required=False,
        type=int,
        help="Time to wait in seconds before saving the values on the scope after they have been reset")
parser.add_argument('-r', '--reboot',
        action='store_true',
        required=False,
        help="If set, reboot the BC WRS between each measure of skewPPS during fine tuning")
parser.add_argument('-i', '--ifdownup',
        action='store_true',
        required=False,
        help="If set, if down then up the BC WRS SFP port between each measure of skewPPS during fine tuning")
parser.add_argument('-c', '--config',
        required=False,
        type=str,
        #type=argparse.FileType('w'),
        help="Name of the file containing the config")

args = parser.parse_args()

# Parse config file if a name has been provided
if(args.config is not None):
    config.set_file(args.config)
    config.parse()
    scope = config.scope
    gm = config.gm
    bc = config.bc
    meas_number = config.meas_number
    meas_duration = config.meas_duration

# Override default number of measures if arg provided
if(args.number is not None):
    meas_number = args.number

# Override default duration of measures if arg provided
if(args.duration is not None):
    meas_duration = args.duration

# Enable reboot between measures if flag is set
if(args.reboot == True):
    bc.reboot_en = args.reboot
if(args.ifdownup == True):
    bc.ifdownup_en = args.ifdownup

delta = False #args.delta

# Set the name of logging file and open it
logger.set_file_name(args.log)
logger.open()

# Copy tx_phase_cal from specified switch
if(args.export is not None):
    print("INFO: scp tx_phase_cal.conf from {}: ".format(args.export))
    res = os.system("scp root@{}:/usr/wr/etc/tx_phase_cal.conf ./tx_phase_cal-{}.conf 1>/dev/null 2>/dev/null".format(args.export, args.export))
    if(res != 0):
        print("ERROR: failed to scp tx_phase_cal.conf from {}".format(args.export))
        quit()
    print("OK")
    while True:
        daddr = get_addr("Destination WRS for tx_phase_cal.conf ? ")
        res = os.system("scp ./tx_phase_cal-{}.conf root@{}:/usr/wr/etc/tx_phase_cal.conf 1>/dev/null 2>/dev/null".format(args.export, daddr))
        if(res != 0):
            print("ERROR: failed to scp tx_phase_cal.conf from {}".format(args.export))
        yn = get_yes_no("Continue export ?")
        if(yn == False):
            break
    yn = get_yes_no("Continue calibration ?")
    if(yn == False):
        quit()

## Get WRSes info ##
if(gm.check_address() == False):
    gm.address = get_addr("Enter GM address? ")
if(gm.ssh_connect() == False):
    print("ERROR: could not connect SSH to {}".format(gm.address))
    quit()
if(gm.check_sfp_port() == False):
    gm.sfp_port = get_int_input("Enter GM port number (1-18)? ", 1, 18)

if(bc.check_address() == False):
    bc.address = get_addr("Enter BC address? ")
if(bc.ssh_connect() == False):
    print("ERROR: could not connect SSH to {}".format(bc.address))
    quit()
if(bc.check_sfp_port() == False):
    bc.sfp_port = get_int_input("Enter BC port number (1-18)? ", 1, 18)

gm.print_info()
bc.print_info()

# No t24p measure, warn user before starting calibration
if(args.t24p is None):
    yn = get_yes_no("Has t24p been measured for each port before starting calibration ?")
    if(yn == False):
        args.t24p = get_int_input("Port to start measuring t24p ?", 1, 18)

## If t24p flag, get t24p, starting from specified port (paramter provided after --t24p) ##
if(args.t24p is not None):
    t24p = [0] * 32
    print("INFO: configuring BC {} to measure t24p: ".format(bc.address), end="", flush=True)
    bc.set_mode_BC()
    for i in range(args.t24p, 19):
        bc.set_port_slave(i)
        print(".", end="", flush=True)
    bc.restart_hald = True
    apply_config(gm, bc)
    bc.is_bc()
    print("OK")
    logger.write_t24p_header()
    for i in range(args.t24p, 19):
        last_port = i
        input("Connect port {} of {} to a valid master then press ENTER".format(i, bc.address))
        print("INFO: measuring t24p for port {}: ".format(i), end="", flush=True)
        t24p[i] = bc.get_port_t24p(i)
        logger.write_t24p(i, t24p[i])
        print("OK")
        print("INFO: writing port {} t24p ({}) in dot-config: ".format(i, t24p[i]), end="", flush=True)
        bc.set_port_t24p(i, t24p[i])
        bc.restart_hald = True
        print("OK")
        if(i < 18):
            yn = get_yes_no("Continue measuring t24p for port {} ?".format(i+1))
            if(yn == False):
                break

    yn = get_yes_no("Apply changes ?")
    if(yn == True):
        print("Applying new t24p values on {}:".format(bc.address), end="", flush=True)
        gm.restart_hald = True
        apply_config(gm, bc)
        print("OK")

    yn = get_yes_no("Continue calibration ?")
    if(yn == False):
        quit()

# If alpha flag, set input alpha value to both GM and BC
if(args.alpha is not None):
    print("INFO: write alpha = {} in dot-config on GM {} and BC {}".format(args.alpha, gm.address, bc.address))
    gm.set_alpha(args.alpha)
    bc.set_alpha(args.alpha)
    gm.restart_hald = True
    bc.restart_hald = True
    yn = get_yes_no("Apply parameters ?")
    if(yn == True):
        print("INFO: applying config on both WRSes: ", end="", flush=True)
        apply_config(gm, bc)
        print("OK")
    yn = get_yes_no("Continue calibration ?")
    if(yn == False):
        quit()

# Check if a scope url/address has been provided in config file
if(scope.check_address() == False):
    yn = get_yes_no("Scope MSO64B available ?")
    if(yn == True):
        scope.address = get_addr("Enter scope address? ")

# Try connect to scope
if(scope.check_address() == True):
    scope.link_api()
    if(scope.connected == True):
        print("INFO: scope {} connected".format(scope.address))
        # Set config
        print("INFO: set scope config")
        scope.set_config()
        scope.print_info()
    else:
        print("ERROR: could not connect to scope {}".format(scope.address))
        yn = get_yes_no("Continue ?")
        if(yn == False):
            quit()

# Write header (info) in header
logger.write_header()


###################################
# 1 - Calculate delta1 and delta2 #
###################################
if(delta == False):
    delta = get_yes_no("Calculate delta1/delta2 ?")

if(delta == True):
    # Set GM as GM (if it isn't), reset alpha and delays to 0
    print("INFO: configuring GM {}: ".format(gm.address), end="", flush=True)
    gm.set_mode_GM()
    for i in range(1, 19):
        gm.set_port_master(i)
    gm.set_alpha(0)
    gm.set_port_delays(gm.sfp_port, 0, 0)
    gm.restart_hald = True
    print("OK")

    print("INFO: configuring BC {}: ".format(bc.address), end="", flush=True)
    bc.set_mode_BC()
    bc.set_alpha(0)
    bc.set_port_slave(bc.sfp_port)
    bc.set_port_delays(bc.sfp_port, 0, 0)
    bc.restart_hald = True
    print("OK")

    print("INFO: applying config on GM & BC: ", end="", flush=True)
    apply_config(gm, bc)
    print("OK")

    """
    print("INFO: waiting for WRSes to boot: ", end="", flush=True)
    while True:
        gm_state = gm.ssh_connect()
        bc_state = bc.ssh_connect()
        if gm_state and bc_state:
            break
        time.sleep(1)
    print("OK")
    """

    print("INFO: checking WRSes configuration: ", end="", flush=True)
    gm_state = gm.is_gm();
    bc_state = bc.is_bc();
    if gm_state and bc_state:
        print("OK")
    else:
        print("FAIL: {} GM = {}, {} BC = {}".format(gm.address, gm_state, bc.address, bc_state))
        quit()

    print("INFO: waiting for GM PLL lock: ", end="", flush=True)
    gm.wait_until_locked(500)
    print("OK")

    input("Plug the short fiber between {} port {} and {} port {} then press ENTER to continue...".format(gm.address, gm.sfp_port, bc.address, bc.sfp_port))
    print("INFO: waiting for BC PLL lock: ", end="", flush=True)
    bc.wait_until_locked(500)
    print("OK")
    print("INFO: rawDelayMM measure: ", end="", flush=True)
    rawDelayMM_short = bc.get_rawDelayMM_mean(bc.sfp_port, delay_meas_nb)
    print("OK")
    print("INFO: delayMM_short = {} s".format(str(rawDelayMM_short)))

    input("Plug the long fiber between {} port {} and {} port {} then press ENTER to continue...".format(gm.address, gm.sfp_port, bc.address, bc.sfp_port))
    print("INFO: waiting for BC PLL lock: ", end="", flush=True)
    bc.wait_until_locked(500)
    print("OK")
    print("INFO: rawDelayMM measure: ", end="", flush=True)
    rawDelayMM_long = bc.get_rawDelayMM_mean(bc.sfp_port, delay_meas_nb)
    print("OK")
    print("INFO: delayMM_long = {} s".format(str(rawDelayMM_long)))

    input("Plug short + long fibers between {} port {} and {} port {} then press ENTER to continue...".format(gm.address, gm.sfp_port, bc.address, bc.sfp_port))
    print("INFO: waiting for BC PLL lock: ", end="", flush=True)
    bc.wait_until_locked(500)
    print("OK")
    print("INFO: rawDelayMM measure: ", end="", flush=True)
    rawDelayMM_combined = bc.get_rawDelayMM_mean(bc.sfp_port, delay_meas_nb)
    print("OK")
    print("INFO: delayMM_combined = {} s".format(str(rawDelayMM_combined)))

    delta1 = rawDelayMM_combined - rawDelayMM_long
    delta2 = rawDelayMM_combined - rawDelayMM_short
    delayTxRx = round(((rawDelayMM_short-delta1)/4)*1e12)

    print("INFO:")
    print("  - rawDelayMM (short)...: {} s".format(rawDelayMM_short))
    print("  - rawDelayMM (long)... : {} s".format(rawDelayMM_long))
    print("  - rawDelayMM (combined): {} s".format(rawDelayMM_combined))
    print("  - delta1...............: {} s".format(delta1))
    print("  - delta2...............: {} s".format(delta2))
    print("  - delayTx/Rx new cal...: {} ps".format(delayTxRx))

else:
    yn = get_yes_no("Values of rawDelayMM for short, long and combined fibers available ?")
    if(yn == True):
        rawDelayMM_short = get_float_input("rawDelayMM short fiber (s) ?", 0, 1)
        rawDelayMM_long = get_float_input("rawDelayMM long fiber (s) ?", 0, 1)
        rawDelayMM_combined = get_float_input("rawDelayMM combined fiber (s) ?", 0, 1)
        delta1 = rawDelayMM_combined - rawDelayMM_long
        delta2 = rawDelayMM_combined - rawDelayMM_short
        delayTxRx = round(((rawDelayMM_short-delta1)/4)*1e12)
        delta = True
    else:
        # Default delta1 value for calibration short fiber
        delta1 = 4.908814999996663e-08
        yn = get_yes_no("Use default short fiber delta1 value: {} s ?".format(delta1))
        if(yn == False):
            delta1 = get_float_input("Enter delta1 (short fiber, s) ?", 0, 1)
        yn = get_yes_no("Get rawDelayMM short fiber to turn GM {} into calibrator ?".format(gm.address))
        if(yn == True):
            set_wrs_config(gm, bc)
            print("INFO: waiting for BC PLL lock: ", end="", flush=True)
            bc.wait_until_locked(500)
            print("OK")
            print("INFO: rawDelayMM measure: ", end="", flush=True)
            rawDelayMM_short = bc.get_rawDelayMM_mean(bc.sfp_port, delay_meas_nb)
            print("OK")
            print("INFO: delayMM_short = {} s".format(str(rawDelayMM_short)))
            delayTxRx = round(((rawDelayMM_short-delta1)/4)*1e12)
            print("INFO: calculated delay Tx/Rx: {} ps".format(delayTxRx))
        else:
            delayTxRx = 0


#########################################
# 2 - Calculate fiber alpha coefficient #
#########################################
alpha = 0
# Values from deltas calculation are needed to calculate alpha
if(delta == True):
    yn = get_yes_no("Calculate alpha ?")
    if (yn == True):
        # Get PPS delay for short fiber
        input("Plug SHORT fiber between GM and BC, then press ENTER")
        print("INFO: waiting for BC PLL lock: ", end="", flush=True)
        bc.wait_until_locked(500)
        print("OK")
        if(scope.connected == False):
            skewPPS1 = get_float_input("Enter skewPPS1 (tPPS_slave - tPPS_master, SHORT fiber) from scope (s) ?", 0, 1)
        else:
            scope.auto_adjust()
            input("Wait until scope measure stable then press ENTER")
            skewPPS1 = float(scope.api.get_mean(1))
        # Get PPS delay for long fiber
        input("Plug LONG fiber between GM and BC, then press ENTER")
        print("INFO: waiting for BC PLL lock: ", end="", flush=True)
        bc.wait_until_locked(500)
        print("OK")
        if(scope.connected == False):
            skewPPS2 = get_float_input("Enter skewPPS2 (tPPS_slave - tPPS_master, LONG fiber) from scope (s) ?", 0, 1)
        else:
            scope.auto_adjust()
            input("Wait until scope measure stable then press ENTER")
            skewPPS2 = float(scope.api.get_mean(1))
        # Calculate alpha
        alpha = (2*(skewPPS2-skewPPS1)) / ((delta2/2) - (skewPPS2 - skewPPS1))
        alpha = round(alpha, 8)
        print("INFO:")
        print("  - skewPPS1 (short fiber): {} s".format(skewPPS1))
        print("  - skewPPS2 (long fiber).: {} s".format(skewPPS2))
        print("  - calulated alpha.......: {}".format(alpha))

yn = get_yes_no("Update alpha in dot-config of GM {} and BC {} ?".format(gm.address, bc.address))
if(yn == True):
    if(alpha == 0):
        alpha = get_float_input("Alpha value ?", 0, 1)

    print("INFO: applying alpha ({}) on both WRSes: ".format(alpha), end="", flush=True)
    gm.set_alpha(alpha)
    bc.set_alpha(alpha)
    gm.restart_hald = True
    bc.restart_hald = True
    print("OK")
    yn = get_yes_no("Apply changes ?")
    if(yn == True):
        apply_config(gm, bc)
        print("OK")


#############################################################
# 3 - Prepare GM (if new calibrator) and BC for calibration #
#############################################################
# Only if egress / ingress values are not 0, propose to apply them to calibrator
if(delayTxRx > 0):
    yn = get_yes_no("Apply delay Tx / Rx ({} ps) to turn GM {} into new calibrator ?".format(delayTxRx, gm.address))
    if(yn == True):
        print("INFO: configuring GM {} port 1 Tx/Rx delays ({} ps): ".format(gm.address, delayTxRx), end="", flush=True)
        gm.set_port_delays(1, delayTxRx, delayTxRx)
        gm.restart_ppsi = True
        print("OK")
    else:
        print("INFO: leaving GM {} (calibrator) as it is".format(gm.address))
        """
        input("INFO: plug the new calibrator (SFP in port 1) and press ENTER")
        gm.golden = get_yes_no("Is the calibrator the golden calibrator (old version not compatible) ?")
        if(gm.golden == False):
            gm.address = get_addr("Enter new GM (calibrator) address ?")
            gm.ssh_connect()
        """

# Set all 18 ports of BC as slave and reset all ingress / egress values
print("INFO: set BC {} ports as slave and reset all delays: ".format(bc.address), end="", flush=True)
for i in range(1, 19):
    bc.set_port_slave(i)
    bc.set_port_delays(i, 0, 0)
    print(".", end="", flush=True)
bc.restart_ppsi = True
print("OK")

# Apply config to both switches
print("INFO: applying changes: ", end="", flush=True)
apply_config(gm, bc)
print("OK")

# Check configuration of both swicthes
print("INFO: checking WRSes configuration: ", end="", flush=True)
if(gm.golden == True):
    gm_state = True
else:
    gm_state = gm.is_gm()
bc_state = bc.is_bc()
if gm_state and bc_state:
    print("OK")
else:
    print("FAIL: {} GM = {}, {} BC = {}".format(gm.address, gm_state, bc.address, bc_state))
    quit()

# Wait for GM PLL to lock
if(gm.golden == False):
    print("INFO: waiting for GM {} PLL lock: ".format(gm.address), end="", flush=True)
    gm.wait_until_locked(500)
    print("OK")


#############################
# 4 - Calibrating all ports #
#############################
# Ingress / egress values for port 1, normal SFP config (Blue Optics, Purple to Blue - P2B)
deltaTxP1_P2B = 0
deltaRxP1_P2B = 0
while True:
    # Only ask during the first loop, if we want to calibrate p1, or only recalibrate 
    # SFPs on p1 using already existing egress / ingress values for p1
    # Check that deltaTxP1_P2B and deltaRxP1_P2B are 0 meaning first loop (calibrtion of p1 itself, not SFP correction)
    if( (bc.sfp_port == 1) and (deltaTxP1_P2B == 0) and (deltaRxP1_P2B == 0) ):
        yn = get_yes_no("Manually input port 1 egress / ingress to calculate only SFP correction ?")
    else:
        yn = False
    
    # We want to skip first calibration of p1, input existing values and calibrate SFP corrections
    if(yn == True):
        deltaTx = get_int_input("Egress latency (ps) ?", 0, 400000)
        deltaRx = get_int_input("Ingress latency (ps) ?", 0, 400000)
    # We are either: calibrating p1, calibrating SFPs on p1, or calibrating other ports
    else:
        # Connect the switch to be calibrated to port 1 of the calibrator
        input("Plug the short fiber between port 1 of calibrator and port {} of {} then press ENTER to continue...".format(bc.sfp_port, bc.address))

        # Wait until BC is synchronized
        print("INFO: waiting for BC {} PLL lock: ".format(bc.address), end="", flush=True)
        bc.wait_until_locked(500)
        print("OK")
        
        # Get Master PHY delays Tx / Rx
        print("INFO: get master PHY delay Tx / Rx from BC {}: ".format(bc.address), end="", flush=True)
        (delayTx, delayRx) = bc.get_masterDelays(bc.sfp_port)
        print("OK")

        # Get rawDelayMM
        print("INFO: get rawDelayMM from BC {}: ".format(bc.address), end="", flush=True)
        rawDelayMM = get_rawDelayMM_mean_log(logger, bc, delay_meas_nb)
        print("OK")

        print("INFO: BC {} port {}".format(bc.address, bc.sfp_port))
        print("  - master delay Tx: {} s".format(delayTx))
        print("  - master delay Rx: {} s".format(delayRx))
        print("  - rawDelayMM.....: {} s".format(rawDelayMM))
        
        # Get bitslide if port is > 12
        bitslide = 0
        if(bc.sfp_port > 12):
            bitslide = bc.get_bitslide(bc.sfp_port)
            print("  - bitslide.......: {} s".format(bitslide))

        # Calculate average delta TxS / RxS
        deltaTxRx = ((rawDelayMM - delayTx - delayRx - bitslide - delta1) / 2) * 1e12
        deltaTx = int(round(deltaTxRx))
        deltaRx = int(round(deltaTxRx))

        # Apply delta to the port and apply config
        print("INFO: applying delayTx = {} ps and delayRx = {} ps on BC {} port {}: ".format(deltaTx, deltaRx, bc.address, bc.sfp_port), end="", flush=True)
        bc.set_port_delays(bc.sfp_port, deltaTx, deltaRx)
        bc.restart_ppsi = True
        apply_config(gm, bc)
        bc.wait_until_locked(500)
        time.sleep(1)
        print("OK")

        # First iteration (coarse), measure skewPPS and update delays
        if(scope.connected == True):
            scope.auto_adjust()
            time.sleep(5)
            skew = float(scope.api.get_mean(1))
            nsamples = int(scope.api.get_nsamples(1))
            print("INFO: first iteration (coarse): measured mean skewPPS = {} s ({} samples)".format(skew, nsamples))
        else:
            input("Clear scope values then press ENTER")
            skew = get_float_input("Enter skewPPS (tPPS_slave - tPPS_master) (s) ?", 0, 1)

        deltaTx = int(round((deltaTx - (skew * 1e12))))
        deltaRx = int(round((deltaRx + (skew * 1e12))))

        print("INFO: writing BC {} port {} delays in dot config:".format(bc.address, bc.sfp_port))
        print("  - deltaTx (egress latency) : {} ps".format(deltaTx))
        print("  - deltaRx (ingress latency): {} ps".format(deltaRx))
        bc.set_port_delays(bc.sfp_port, deltaTx, deltaRx)

        # if reboot mode is not enable, apply parameters here
        if(bc.reboot_en == False):
            print("INFO: applying changes on BC {}: ".format(bc.address), end="", flush=True)
            bc.restart_ppsi = True
            apply_config(gm, bc)
            bc.wait_until_locked(500)
            time.sleep(1)
            print("OK")

        logger.write_meas_header()

        # Second iteration (fine), measure skewPPS, reboot BC, log measures and update delays
        print("INFO: starting second iteration (fine tune), {} measures of {} s".format(meas_number, meas_duration))
        curr_meas_nb = 0
        total_skew = 0
        while(curr_meas_nb < meas_number):
            curr_meas_nb += 1
            if(bc.reboot_en == True):
                print("INFO: rebooting BC {}: ".format(bc.address), end="", flush=True)
                while(bc.is_up() == True):
                    bc.reboot()
                    print(".", end="", flush=True)
                    time.sleep(1)
                print("down", end="", flush=True)
                while(bc.is_up() == False):
                    print(".", end="", flush=True)
                    #time.sleep(1)
                print("up", end="", flush=True)
                while(bc.ssh_connect() == False):
                    print(".", end="", flush=True)
                    time.sleep(1)
                print("SSH connected: OK")
            elif(bc.ifdownup_en == True):
                print("INFO: ifdownup BC {} port {}: ".format(bc.address, bc.sfp_port), end="", flush=True)
                bc.ifdownup()
                print("OK")

            print("INFO: waiting for BC {} PLL lock: ".format(bc.address), end="", flush=True)
            bc.wait_until_locked(500)
            print("OK")

            if(scope.connected == True):
                scope.auto_adjust()
                print("INFO: waiting {} s".format(meas_duration))
                time.sleep(meas_duration)
                skew = float(scope.api.get_mean(1))
                total_skew += skew
                nsamples = int(scope.api.get_nsamples(1))
                logger.write_measure(curr_meas_nb)
                print("INFO: -- saving measure #{} (skewPPS mean = {} s, {} samples, average: {} s) --".format(curr_meas_nb, skew, nsamples, total_skew/curr_meas_nb))
                #print("INFO: first iteration (coarse) skewPPS mean from scope: {} s ({} samples)".format(skew, nsamples))
            else:
                input("Clear scope values then press ENTER")
                skew = get_float_input("Enter skewPPS (tPPS_slave - tPPS_master) (s) ?", 0, 1)

        skew = total_skew / meas_number
        logger.write_measure_average(skew)
        print("INFO: {} measures taken, average skew: {} s".format(meas_number, skew))

        deltaTx = int(round((deltaTx - (skew * 1e12))))
        deltaRx = int(round((deltaRx + (skew * 1e12))))

    print("INFO: writing BC {} dot-config:".format(bc.address))
    print("  - deltaTx (egress latency) : {} ps".format(deltaTx))
    print("  - deltaRx (ingress latency): {} ps".format(deltaRx))
    bc.set_port_delays(bc.sfp_port, deltaTx, deltaRx)
    logger.write_egress_ingress(deltaTx, deltaRx)

    yn = get_yes_no("Apply values on BC {} port {} ?".format(bc.address, bc.sfp_port))
    if(yn == True):
        print("INFO: applying values on BC {} port {}: ".format(bc.address, bc.sfp_port), end="", flush=True)
        bc.restart_ppsi = True
        apply_config(gm, bc)
        bc.wait_until_locked(500)
        print("OK")

    # If current port is port 18, then calibration is over
    if(bc.sfp_port == 18):
        break

    # Only on port 1, calculate SFPs Tx/Rx correction values
    elif(bc.sfp_port == 1):
        # Port 1 calibrated, we assume that:
        # - it was with default SFP type (BlueOptics - BO)
        # - the SFPs were in the "normal" config: purple/orange on GM to blue on BC
        # Save calibration values of port 1
        if(deltaTxP1_P2B == 0 and deltaRxP1_P2B == 0):
            deltaTxP1_P2B = deltaTx
            deltaRxP1_P2B = deltaRx
            #### TODO: write SFP correction header here, to indicate which SFP are in use during the first calibration (base values)
        # We already have the calibration values for default (blue BO) SFP in normal config
        # So just calculate the correction values for the current SFP config
        else:
            print("INFO: alternative SFP config, calibration values:")
            print("  - SFP Tx correction: {} ps".format(deltaTx-deltaTxP1_P2B))
            print("  - SFP Rx correction: {} ps".format(deltaRx-deltaRxP1_P2B))
            logger.write_sfp_correction_header()
            logger.write_sfp_correction(deltaTx-deltaTxP1_P2B, deltaRx-deltaRxP1_P2B) 
            yn = get_yes_no("Auto detect SFP index in database ?")
            sfp_index = -1
            # Match SFP part number in port 1 with SFP database and get corresponding database entry index
            if(yn == True):
                sfp_pn = bc.get_sfp_pn(1)
                print("INFO: BC {}, port {}, SFP PN: {}".format(bc.address, bc.sfp_port, sfp_pn))
                sfp_index = bc.get_sfp_db_index(1)
                if(sfp_index == -1):
                    print("INFO: no entry found in database for SFP PN {}".format(sfp_pn))
                else:
                    print("INFO: SFP PN {} found in database, index: {}".format(sfp_pn, sfp_index))
            # Ask user to input database entry index manually, allows to skip updating database
            else:
                sfp_index = get_int_input("Enter SFP index (-1 to skip)?", -1, 100)
            # Check that database entry index exists in database
            index_valid = bc.check_sfp_index(sfp_index)
            # Index corresponds to a database entry, update the values
            if( (sfp_index >= 0) and (index_valid == True) ):
                line = bc.ssh_command("cat /usr/wr/etc/dot-config | grep CONFIG_SFP{:02d}_PARAMS=".format(int(sfp_index)))
                print("INFO: SFP database line to be updated (SFP index = {}):".format(sfp_index))
                print(line)
                bc.set_sfp_param(sfp_index, deltaTx-deltaTxP1_P2B, deltaRx-deltaRxP1_P2B)
                line = bc.ssh_command("cat /usr/wr/etc/dot-config | grep CONFIG_SFP{:02d}_PARAMS=".format(int(sfp_index)))
                print("INFO: updated line:")
                print(line)
                yn = get_yes_no("Apply changes ?")
                if(yn == True):
                    bc.restart_hald = True
                    apply_config(gm, bc)
            # Index does not correspond to any database entry
            elif(index_valid == False):
                print("INFO: index {} does not exist, no changes applied to SFP database".format(sfp_index))
            # Skip database update (user manually entered -1)
            else:
                print("INFO: skipping SFP database update, no changes applied")
        
        yn = get_yes_no("Calibrate other SFP config (SFP correction value) ?")
        if(yn == True):
            print("INFO: reset BC {} port {} delays: ".format(bc.address, bc.sfp_port), end="", flush=True)
            bc.set_port_delays(bc.sfp_port, 0, 0)
            bc.restart_ppsi = True
            apply_config(gm, bc)
            # Not needed because we loop and check for sync at the beginning of the loop
            #bc.wait_until_locked(500)
            print(" OK")
            input("Plug the SFP on port 1 of BC {} (match color of SFP on port 1 of GM {}), then press ENTER".format(bc.address, gm.address))
        else:
            # Set back values of first SFP calibrated (assumed to be default BlueOptics SFP)
            # in case we calibrated other SFPs to get tx/rx parameters
            bc.set_port_delays(bc.sfp_port, deltaTxP1_P2B, deltaRxP1_P2B)
            bc.sfp_port = bc.sfp_port + 1
            yn = get_yes_no("Continue calibration for port {} ?".format(bc.sfp_port))
            if(yn == False):
                break
    
    # Not p1 and not p18, just continue calibration for next port, or not
    else:
        bc.sfp_port = bc.sfp_port + 1
        yn = get_yes_no("Continue calibration for port {} ?".format(bc.sfp_port))
        if(yn == False):
            break

quit()

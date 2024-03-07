#!/bin/ash
export WR_HOME="/wr"

insmod $WR_HOME/lib/modules/asix.ko
if [ -e /proc/sys/net/ipv4/conf/eth1 ]; then
	echo "Using DHCP to get IP for eth1"
	udhcpc -b -i eth1
fi

LOAD_FPGA_STATUS_FILE="/tmp/load_fpga_status"
LOAD_LM32_STATUS_FILE="/tmp/load_lm32_status"
#files for monit's restart reason
MONIT_RR_FLASH="/update/monit_restart_reason"
MONIT_RR_TMP="/tmp/monit_restart_reason"

# Get parameter from kernel commandline
for arg in $(cat /proc/cmdline); do
	echo $arg | grep -q "wr_nic.macaddr" ;
	if [ $? == 0 ]; then
		val=$(echo $arg | cut -d= -f2);
	fi;
done

# handle monit's restat reason
# no need to remove $MONIT_RR_TMP, since tmp is not persistent
if [ -f "$MONIT_RR_FLASH" ]; then
    # move restart reason to tmp so there is no need to remove it later
    mv -f "$MONIT_RR_FLASH" "$MONIT_RR_TMP"
fi

# Turn on yellow LED
# When the linux has boot the status led
# is unpowered so set it to yellow
# until the HAL start (LED is orange) and finally
# WR is setup (LED is green)
$WR_HOME/bin/wrs_status_led -y

# Obtain the type of FPGA (LX130XT or LX240XT)
tfpga=$($WR_HOME/bin/wrs_version -f)
if [ "$tfpga" = "UNKNOWN" ]; then
    tfpga="LX240T"
fi
FP_FILE="$WR_HOME/lib/firmware/18p_mb-${tfpga}.bin"


# get scb version
scb_ver=$($WR_HOME/bin/wrs_version -s)
if [ "$scb_ver" = "UNKNOWN" ]; then
    echo "Warning: UNKNOWN scb version! default to 3.3" >& 2
    scb_ver=33
fi
LM_FILE="$WR_HOME/lib/firmware/rt_cpu.elf"

if ! [ -f "$FP_FILE" ]; then
    echo "Fatal: can't find \"$FP_FILE\"" >& 2
    echo "load_file_not_found" > $LOAD_FPGA_STATUS_FILE
    exit 1;
fi
if ! [ -f "$LM_FILE" ]; then
    echo "Fatal: can't find \"$LM_FILE\"" >& 2
    echo "load_file_not_found" > $LOAD_LM32_STATUS_FILE
    exit 1;
fi

$WR_HOME/bin/load-virtex $FP_FILE
if [ $? -eq 0 ];
then
    echo "load_ok" > $LOAD_FPGA_STATUS_FILE
    md5sum $FP_FILE | cut -d ' ' -f 1 >> $LOAD_FPGA_STATUS_FILE
else
    echo "Fatal: load FPGA failed" >& 2
    echo "load_error" > $LOAD_FPGA_STATUS_FILE
fi

# FIXME
# Don't try to do **anything** here. The LM32 **must** be programed
# before doing anything else. We do not know yet the reson but without
# the following step the FPGA cannot be access properly

$WR_HOME/bin/load-lm32   $LM_FILE scb_ver=${scb_ver}
if [ $? -eq 0 ];
then
    echo "load_ok" > $LOAD_LM32_STATUS_FILE
else
    echo "Fatal: load LM32 failed" >& 2
    echo "load_error" > $LOAD_LM32_STATUS_FILE
fi
# FIXME also this sleep is necessary because the LM32 does some magic
sleep 1

# At this offset there is an ID. Just check that it's there as a
# small proof that we correctly programmed the FPGA
CHK_ADDR=0x10030034
CHK_EXP=0xcafebabe
CHK_VAL=$(devmem $CHK_ADDR | tr '[:upper:]' '[:lower:]')
if [ $CHK_VAL == $CHK_EXP ]
then
    echo "The FPGA and the LM32 are programmed"
else
    echo "The bitstream $FP_FILE is not correct or there something is not working with the FPGA"
    echo "Expected: [$CHK_ADDR] = $CHK_EXP"
    echo "Current: [$CHK_ADDR] = $CHK_VAL"
    echo "LM32 program and drivers for the bitstream components will not be loaded"
    exit
fi


insmod $WR_HOME/lib/modules/htvic.ko
insmod $WR_HOME/lib/modules/wrs_devices.ko
insmod $WR_HOME/lib/modules/wr-nic.ko macaddr=$val
insmod $WR_HOME/lib/modules/wr_rtu.ko
insmod $WR_HOME/lib/modules/wr_pstats.ko pstats_nports=18
insmod $WR_HOME/lib/modules/wr_clocksource.ko


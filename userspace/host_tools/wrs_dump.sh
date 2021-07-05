#!/bin/bash

# Author: Adam Wujek
# Copyright CERN 2021

TCP_DUMP_TIME=20

showHelp() {
cat << EOF
Usage: $0 [--help] [--ppsi-verbose <verbose_mode>] [--ppsi-verbose-all] user@wr-switch
Dump various information from White-Rabbit Switch.

-h, --help                         Display help
    --ppsi-verbose <verbose_mode>  Run ppsi for 60 seconds with specified verbose mode. The ppsi is not restarted.
                                   Please provide the verbose mode in the format:
                                   0x<fsm><time><frames><servo><bmc><ext><config>0
                                   Do not skip leading 0's. Please also remember about leading "0x" and the following "0".
    --ppsi-verbose-all             Run ppsi for 60 seconds with full verbose (0x33333333).
-o  --output <directory>           Save the output in the given directory.
    --tcpdump <1,2,x,..>           Run tcpdump for $TCP_DUMP_TIME seconds on all ports listed as the comma separated list.
                                   e.g. 1,5,7
    --no-tcpdump                   Don't run tcpdump

Enviroment variables:
WRS_PSWD - If specified, use sshpass with the password stored in this env variable

Script gets the following:
  output of wrs_version
  output of w command (logged users and uptime)
  process list
  output of wrs_dump_shmem
  output of wrs_pstats
  output of wr_mon
  output of df
  output of free
  output of /proc/meminfo
  output of ifconfig
  tcpdump of up ports (on specified ports depending on the parameter)
  output of PPSI's verbose messages (if selected by the parameter)
  output of dmesg
  output of wrs_vlans --list
  output of wrs_vlans --plist
  output of rtu_stat
  output of wrs_sfp_dump -L -d -x
  dot-config
  shmem files
  content of /tmp
  again process list
  again output of wrs_dump_shmem
  again output of wrs_pstats
  again output of wr_mon
  again output of df
  again output of free
  again output of /proc/meminfo
  again output of ifconfig

EOF
}

script_name=$0

options=$(getopt -l "help,ppsi-verbose:,ppsi-verbose-all,output:,tcpdump:,no-tcpdump" -o "ho:" -- "$@")

eval set -- "$options"

while true
do
    case $1 in
    -h|--help)
	showHelp $script_name
	exit 0
	;;
    --ppsi-verbose)
	shift
	#TODO: validate if parametar is a valid number (including HEX)
	export ppsi_verbose_mode="$1"
	;;
    --ppsi-verbose-all)
	export ppsi_verbose_mode="0x33333333"
	;;
    -o|--output)
	shift
	output_dir="$1"
	;;
    --tcpdump)
	shift
	tcpdump_ports="${1//,/ }"
	;;
    --no-tcpdump)
	# don't run tcpdump
	tcpdump_ports="-1"
	;;
    --)
	shift
	break;;
    esac
    shift
done


shift $((OPTIND-1))

if [ x"$1" == x ]; then
    echo "Please provide username@hostname as a parameter like $script_name root@wrs1"
    exit 1
fi
echo $1 ppsi_verbose_mode=$ppsi_verbose_mode
# exit 0

HOSTNAME=$(echo "$1"| cut -d@ -f2)
echo "Open ssh connection... "

if [ -z "$WRS_PSWD" ]
then
    echo "Provide password"
    ssh -N -f -oControlMaster=yes -oControlPath=/tmp/ssh-%r-%h-%p $1
else
    echo "Password provided"
    sshpass -p "$WRS_PSWD" ssh -N -f -oControlMaster=yes -oControlPath=/tmp/ssh-%r-%h-%p $1
fi

echo "ssh connection established"

CUR_DATE=`date "+%F_%H-%M-%S"`
if [ -n "$output_dir" ]; then
    WORK_DIR="$output_dir"
else
    WORK_DIR=wrs_dump-"$HOSTNAME"-"$CUR_DATE"
fi
mkdir -p "$WORK_DIR"

echo "Store data in the directory $WORK_DIR"




#and run your commands
echo -n "Get version... "
ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "/wr/bin/wrs_version -t" > "$WORK_DIR"/wrs_version.txt
echo "Done"

echo -n "Get w (logged users and uptime)... "
ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "/bin/w" > "$WORK_DIR"/w.txt
echo "Done"

echo -n "Get process list... "
ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "ps aux" > "$WORK_DIR"/ps-`date "+%F_%H-%M-%S"`.txt
echo "Done"

# save SHMEM_FILENAME for later
SHMEM_FILENAME="$WORK_DIR"/wrs_dump_shmem-`date "+%F_%H-%M-%S"`.txt
echo -n "Get output of wrs_dump_shmem... "
ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "/wr/bin/wrs_dump_shmem" > "$SHMEM_FILENAME"
echo "Done"

echo -n "Get output of wrs_pstats... "
ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "/wr/bin/wrs_pstats -s -a & sleep 5" > "$WORK_DIR"/wrs_pstats-`date "+%F_%H-%M-%S"`.txt
echo "Done"

echo -n "Get output of wr_mon... "
ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "/wr/bin/wr_mon & sleep 3" > "$WORK_DIR"/wrs_mon-`date "+%F_%H-%M-%S"`.txt
echo "Done"


echo -n "Get output of df... "
ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "/bin/df" > "$WORK_DIR"/df-`date "+%F_%H-%M-%S"`.txt
echo "Done"

echo -n "Get output of free... "
ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "/bin/free" > "$WORK_DIR"/free-`date "+%F_%H-%M-%S"`.txt
echo "Done"

echo -n "Get output of /proc/meminfo... "
ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "/bin/cat /proc/meminfo" > "$WORK_DIR"/meminfo-`date "+%F_%H-%M-%S"`.txt
echo "Done"

echo -n "Get output of ifconfig... "
ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "/sbin/ifconfig" > "$WORK_DIR"/ifconfig-`date "+%F_%H-%M-%S"`.txt
echo "Done"

if ! [ -n "$tcpdump_ports" ]; then
    # autodetect ports that are up
    tcpdump_ports=$(cat "$SHMEM_FILENAME" | grep -i linkup | grep \ 1 | cut -d. -f3)
fi

if [ -n "$tcpdump_ports" ] && [ "-1" != "$tcpdump_ports" ]; then
    echo "NOTE: For tcpdump dumps check in tmp directory" > "$WORK_DIR"/tcpdump.txt
    echo -n "Get tcpdump for up ports:"
    for port_num in $tcpdump_ports; do
	echo -n " wri$port_num"
    done
    echo ""

    for port_num in $tcpdump_ports; do
	echo -n "Get output of tcpdump wri${port_num}... "
	ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "/usr/sbin/tcpdump -i wri${port_num} --immediate-mode -w - >/tmp/tcpdump-wri${port_num}.pcap & sleep $TCP_DUMP_TIME" >> "$WORK_DIR"/tcpdump.txt
	echo "Done"
    done
fi

# verbose=3
# i=0
# while [ $i -lt 8 ]
# do
#     # don't use different verbose modes for now
#     break
#     echo -n "Get logs from ppsi verbose "; printf '0x%08x' $verbose
# 
#     ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "gdb --pid=\`pidof ppsi\` /wr/bin/ppsi -batch -ex 'set pp_global_d_flags = ${verbose}' -ex 'p pp_global_d_flags'" >> "$WORK_DIR"/ppsi.txt
#     j=0
#     while [ $j -lt 20 ]
#     do
# 	echo -n .
# 	sleep 1
# 	j=$[$j+1]
#     done
#     echo "Done"
#     verbose=$[$verbose*16]
#     i=$[$i+1]
# done

if [ -n "$ppsi_verbose_mode" ]; then
    echo -n "Get logs from ppsi verbose $ppsi_verbose_mode"
    # change in runtime the variable pp_global_d_flags in the PPSI using the GDB
    ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "gdb --pid=\`pidof ppsi\` /wr/bin/ppsi -batch -ex 'set pp_global_d_flags=${ppsi_verbose_mode}'" >> "$WORK_DIR"/ppsi.txt
    j=0
    while [ $j -lt 60 ]
    do
	echo -n .
	sleep 1
	j=$[$j+1]
    done
    ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "gdb --pid=\`pidof ppsi\` /wr/bin/ppsi -batch -ex 'set pp_global_d_flags=0x0'" >> "$WORK_DIR"/ppsi.txt
    echo "Done"
fi

echo -n "Get output of dmesg... "
ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "/bin/dmesg" > "$WORK_DIR"/dmesg.txt
echo "Done"

# WRS related

echo -n "Get output of wrs_vlans --list... "
ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "/wr/bin/wrs_vlans --list" > "$WORK_DIR"/wrs_vlans-list.txt
echo "Done"

echo -n "Get output of wrs_vlans --plist... "
ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "/wr/bin/wrs_vlans --plist" > "$WORK_DIR"/wrs_vlans-plist.txt
echo "Done"

echo -n "Get output of rtu_stat... "
ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "/wr/bin/rtu_stat" > "$WORK_DIR"/rtu_stat.txt
echo "Done"

echo -n "Get output of wrs_sfp_dump -L -d -x... "
ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "/wr/bin/wrs_sfp_dump -L -d -x" > "$WORK_DIR"/wrs_sfp_dump.txt
echo "Done"


echo -n "Copy dot-config... "
scp -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1:/wr/etc/dot-config "$WORK_DIR"
echo "Done"

echo -n "Copy shmem... "
mkdir -p  "$WORK_DIR"/shmem
scp -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1:/dev/shm/* "$WORK_DIR"/shmem
echo "Done"

echo -n "Copy /tmp... "
mkdir -p  "$WORK_DIR"/tmp
scp  -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1:/tmp/* "$WORK_DIR"/tmp 2>/dev/null
echo "Done"






echo -n "Get again process list... "
ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "ps aux" > "$WORK_DIR"/ps-`date "+%F_%H-%M-%S"`.txt
echo "Done"

echo -n "Get again output of wrs_dump_shmem... "
ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "/wr/bin/wrs_dump_shmem" > "$WORK_DIR"/wrs_dump_shmem-`date "+%F_%H-%M-%S"`.txt
echo "Done"

echo -n "Get again output of wrs_pstats... "
ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "/wr/bin/wrs_pstats -s -a & sleep 5" > "$WORK_DIR"/wrs_pstats-`date "+%F_%H-%M-%S"`.txt
echo "Done"

echo -n "Get again output of wr_mon... "
ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "/wr/bin/wr_mon & sleep 3" > "$WORK_DIR"/wrs_mon-`date "+%F_%H-%M-%S"`.txt
echo "Done"


echo -n "Get again output of df... "
ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "/bin/df" > "$WORK_DIR"/df-`date "+%F_%H-%M-%S"`.txt
echo "Done"

echo -n "Get again output of free... "
ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "/bin/free" > "$WORK_DIR"/free-`date "+%F_%H-%M-%S"`.txt
echo "Done"

echo -n "Get again output of /proc/meminfo... "
ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "/bin/cat /proc/meminfo" > "$WORK_DIR"/meminfo-`date "+%F_%H-%M-%S"`.txt
echo "Done"

echo -n "Get again output of ifconfig... "
ssh -q -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 -t "/sbin/ifconfig" > "$WORK_DIR"/ifconfig-`date "+%F_%H-%M-%S"`.txt
echo "Done"


echo -n "Closing ssh connection... "
ssh  -O exit -oControlMaster=no -oControlPath=/tmp/ssh-%r-%h-%p $1 2> /dev/null
echo "Done"

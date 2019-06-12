#!/bin/sh

# Script read wrs_auxclk parameters from dot-config, then start wrs_auxclk.

tmpdir=/tmp
wrs_auxclk_set_status_file="$tmpdir"/wrs_auxclk_set_status

# First, read dot-config to get wrs_auxclk parameters
dotconfig=/wr/etc/dot-config
set -o pipefail

if [ -f $dotconfig ]; then
    . $dotconfig
else
    # exit
    echo "dot-config not found! Don't setup wrs_auxclk"
    exit 1
fi

if [ ! -z "$CONFIG_WRSAUXCLK_FREQ" ]; then
	p_freq="--freq "$CONFIG_WRSAUXCLK_FREQ;
fi

if [ ! -z "$CONFIG_WRSAUXCLK_DUTY" ]; then
	p_duty="--duty "$CONFIG_WRSAUXCLK_DUTY;
fi

if [ ! -z "$CONFIG_WRSAUXCLK_CSHIFT" ]; then
	p_cshift="--cshift "$CONFIG_WRSAUXCLK_CSHIFT;
fi

if [ ! -z "$CONFIG_WRSAUXCLK_SIGDEL" ]; then
	p_sigdel="--sigdel "$CONFIG_WRSAUXCLK_SIGDEL;
fi

if [ ! -z "$CONFIG_WRSAUXCLK_PPSHIFT" ]; then
	p_ppshift="--ppshift "$CONFIG_WRSAUXCLK_PPSHIFT;
fi

WRS_LOG=$CONFIG_WRS_LOG_OTHER

# if empty turn it to /dev/null
if [ -z $WRS_LOG ]; then
    WRS_LOG="/dev/null";
fi

# if a pathname, use it
if echo "$WRS_LOG" | grep / > /dev/null; then
    eval LOGPIPE=\" \> $WRS_LOG 2\>\&1 \";
else
    # not a pathname: use verbatim
    eval LOGPIPE=\" 2\>\&1 \| logger -t auxclk -p $WRS_LOG\"
fi

# execute wrs_auxclk
echo -n "Configuring external clock clk2: "
eval /wr/bin/wrs_auxclk $p_freq $p_duty $p_cshift $p_sigdel $p_ppshift $LOGPIPE
ret=$?
if [ $ret -eq 0 ]; then
    echo "OK"
    echo "ok" > $wrs_auxclk_set_status_file
else
    echo "Failed"
    echo "failed" > $wrs_auxclk_set_status_file
fi

#!/bin/sh

tmpdir=/tmp
vlans_set_status_file="$tmpdir"/vlans_set_status

dotconfig=/wr/etc/dot-config

set -o pipefail

start() {
    echo -n "Setting up VLANs: "

    if [ -f $dotconfig ]; then
	. $dotconfig
    else
	echo "$0 unable to start wrs_throttling, unable to source " \
	      "dot-config ($dotconfig)!"
	exit 1
    fi

    WRS_LOG=$CONFIG_WRS_LOG_OTHER

    # if empty turn it to /dev/null
    if [ -z $WRS_LOG ]; then
	WRS_LOG="/dev/null";
    fi

    # if a pathname, use it
    if echo "$WRS_LOG" | grep / > /dev/null; then
	eval LOGPIPE=\" \> $WRS_LOG 2\>\&1 \";
    elif [ "$WRS_LOG" == "default_syslog" ]; then
	# not a pathname: use verbatim
	eval LOGPIPE=\" 2\>\&1 \| logger -t vlan --prio-prefix -p daemon.info\"
    else
	eval LOGPIPE=\" 2\>\&1 \| logger -t vlan -p $WRS_LOG\"
    fi

    # set msg level
    if [ ! -z $CONFIG_WRS_LOG_LEVEL_OTHER ]; then
	WRS_MSG_LEVEL=$CONFIG_WRS_LOG_LEVEL_OTHER
	export WRS_MSG_LEVEL
    fi

    # set-up VLANs
    eval /wr/bin/wrs_vlans -f /wr/etc/dot-config $LOGPIPE
    ret=$?
    if [ $ret -eq 0 ]; then
	echo "OK"
	echo "ok" > $vlans_set_status_file
    elif [ $ret -eq 2 ]; then
	echo "Disabled"
	echo "disabled" > $vlans_set_status_file
    else
	echo "Failed"
	echo "failed" > $vlans_set_status_file
    fi
}

stop() {
    echo -n "Clean VLANs' configuration: "

    /wr/bin/wrs_vlans --port 1-18 --pmode 3
    /wr/bin/wrs_vlans --clear

    echo "OK"
}

restart() {
    stop
    start
}

case "$1" in
    start)
	start
	;;
    stop)
	stop
	;;
    restart|reload)
	restart
	;;
    *)
	echo $"Usage: $0 {start|stop|restart}"
	exit 1
	;;
esac

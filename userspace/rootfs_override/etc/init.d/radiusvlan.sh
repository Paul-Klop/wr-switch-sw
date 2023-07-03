#!/bin/sh

dotconfig=/wr/etc/dot-config

start_counter() {
    # increase start counter
    COUNTER_FILE="/tmp/start_cnt_rvlan"
    START_COUNTER=1
    if [ -f "$COUNTER_FILE" ] ; then
	read -r START_COUNTER < $COUNTER_FILE
	START_COUNTER=$((START_COUNTER+1))
    fi
    echo "$START_COUNTER" > $COUNTER_FILE
}

start() {
    if [ -f $dotconfig ]; then
	. $dotconfig
    else
	echo "$0 unable to source dot-config ($dotconfig)!"
    fi

    if [ "$CONFIG_RVLAN_ENABLE" != "y" ]; then
	echo "Radius-VLAN: disabled"
	exit 0
    fi
    echo -n "Starting Radius-VLAN daemon: "

    WRS_LOG=$CONFIG_WRS_LOG_OTHER

    # if empty turn it to /dev/null
    if [ -z $WRS_LOG ]; then
	WRS_LOG="/dev/null";
    fi

    # if a pathname, use it
    if echo "$WRS_LOG" | grep / > /dev/null; then
	eval LOGPIPE=\" \> $WRS_LOG 2\>\&1 \";
    elif [ "$WRS_LOG" == "default_syslog" ]; then
	eval LOGPIPE=\" 2\>\&1 \| logger -t rvlan -p daemon.info\"
    else
	# not a pathname: use verbatim
	eval LOGPIPE=\" 2\>\&1 \| logger -t rvlan -p $WRS_LOG\"
    fi

    # pidof: no running script should have the same name as this process
    if pidof radiusvlan > /dev/null; then
	echo "Failed (already running?)"
    else
	eval /wr/bin/radiusvlan $LOGPIPE \&
	start_counter
	echo "OK"
    fi
}

stop() {
    echo -n "Stopping Radius-VLAN "
    start-stop-daemon -K -q --exec /wr/bin/radiusvlan
    if [ $? -eq 0 ]; then
	echo "OK"
    else
	echo "Failed"
    fi
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

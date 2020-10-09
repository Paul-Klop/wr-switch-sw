#!/bin/sh

dotconfig=/wr/etc/dot-config

start() {
    if [ -f $dotconfig ]; then
	. $dotconfig
    else
	echo "$0 unable to source dot-config ($dotconfig)!"
    fi

    if [ "$CONFIG_RVLAN_DAEMON" = "n" ]; then
	echo "Radius-VLAN: disabled\n"
	return
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

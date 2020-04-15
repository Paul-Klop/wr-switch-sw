#!/bin/sh

dotconfig=/wr/etc/dot-config

start() {
    echo -n "Starting RTUd daemon: "

    if [ -f $dotconfig ]; then
	. $dotconfig
    else
	echo "$0 unable to source dot-config ($dotconfig)!"
    fi

    WRS_LOG=$CONFIG_WRS_LOG_RTU

    # if empty turn it to /dev/null
    if [ -z $WRS_LOG ]; then
	WRS_LOG="/dev/null";
    fi

    # if a pathname, use it
    if echo "$WRS_LOG" | grep / > /dev/null; then
	eval LOGPIPE=\" \> $WRS_LOG 2\>\&1 \";
    elif [ "$WRS_LOG" == "default_syslog" ]; then
	# not a pathname: use verbatim
	eval LOGPIPE=\" 2\>\&1 \| logger -t rtud --prio-prefix -p daemon.info\"
    else
	# not a pathname: use verbatim
	eval LOGPIPE=\" 2\>\&1 \| logger -t rtud -p $WRS_LOG\"
    fi

    # set msg level
    if [ ! -z $CONFIG_WRS_LOG_LEVEL_RTU ]; then
	WRS_MSG_LEVEL=$CONFIG_WRS_LOG_LEVEL_RTU
	export WRS_MSG_LEVEL
    fi

    # be carefull with pidof, no running script should have the same name as
    # process
    if pidof rtud > /dev/null; then
	# rtud already running
	echo "Failed (already running?)"
    else
	eval /wr/bin/wrsw_rtud $LOGPIPE \&
	echo "OK"
    fi
}

stop() {
    echo -n "Stopping RTUd "
    start-stop-daemon -K -q --exec /wr/bin/wrsw_rtud
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

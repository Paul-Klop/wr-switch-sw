#!/bin/sh

dotconfig=/wr/etc/dot-config

start() {
    echo -n "Starting PTP daemon: "

    if [ -f $dotconfig ]; then
	. $dotconfig
    else
	echo "$0 unable to source dot-config ($dotconfig)!"
    fi

    WRS_LOG=$CONFIG_WRS_LOG_PTP

    # if empty turn it to /dev/null
    if [ -z $WRS_LOG ]; then
	WRS_LOG="/dev/null";
    fi

    # set msg level
    if [ ! -z $CONFIG_WRS_LOG_LEVEL_PTP ]; then
	LOG_LEVEL="-d $CONFIG_WRS_LOG_LEVEL_PTP"
    fi

    # if a pathname, use it
    if echo "$WRS_LOG" | grep / > /dev/null; then
	eval LOGPIPE=\" \> $WRS_LOG 2\>\&1 \";
    elif [ "$WRS_LOG" == "default_syslog" ]; then
	# not a pathname: use verbatim
	eval LOGPIPE=\" 2\>\&1 \| logger -t ppsi --prio-prefix\"
    else
	# not a pathname: use verbatim
	eval LOGPIPE=\" 2\>\&1 \| logger -t ppsi -p $WRS_LOG\"
    fi

# be carefull with pidof, no running script should have the same name as process
    if pidof ppsi > /dev/null; then
	# ppsi already running
	echo "Failed (already running?)"
    else
	eval /wr/bin/ppsi $LOG_LEVEL $LOGPIPE \&
	echo "OK"
    fi
}

stop() {
    echo -n "Stopping PTP: "
    start-stop-daemon -K -q --exec /wr/bin/ppsi
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

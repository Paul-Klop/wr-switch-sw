#!/bin/sh

WDG_PID=/var/run/wrs_watchdog.pid
dotconfig=/wr/etc/dot-config

start_counter() {
	# increase boot counter
	COUNTER_FILE="/tmp/start_cnt_wrs_watchdog"
	START_COUNTER=1
	if [ -f "$COUNTER_FILE" ];
	then
	    read -r START_COUNTER < $COUNTER_FILE
	    START_COUNTER=$((START_COUNTER+1))
	fi
	echo "$START_COUNTER" > $COUNTER_FILE
}

start() {
    echo -n "Starting wrs_watchdog daemon: "

    if [ -f $WDG_PID ] && kill -0 `cat $WDG_PID` &> /dev/null; then
	# wrs_watchdog already running
	echo "Failed (already running?)"
    else
	if [ -f $dotconfig ]; then
	    . $dotconfig
	else
	    echo "$0 unable to source dot-config ($dotconfig)!"
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
	    eval LOGPIPE=\" 2\>\&1 \| logger -t watchdog --prio-prefix -p daemon.info\"
	else
	    # not a pathname: use verbatim
	    eval LOGPIPE=\" 2\>\&1 \| logger -t watchdog -p $WRS_LOG\"
	fi

	# set msg level
	if [ ! -z $CONFIG_WRS_LOG_LEVEL_OTHER ]; then
	    WRS_MSG_LEVEL=$CONFIG_WRS_LOG_LEVEL_OTHER
	    export WRS_MSG_LEVEL
	fi

	eval /wr/bin/wrs_watchdog -d -p $WDG_PID $LOGPIPE \&
	start_counter
	echo "OK"
    fi
}

stop() {
    echo -n "Stopping wrs_watchdog "
    start-stop-daemon -K -q -p $WDG_PID
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

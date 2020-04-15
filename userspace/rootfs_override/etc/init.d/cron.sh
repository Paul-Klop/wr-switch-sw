#!/bin/sh

dotconfig=/wr/etc/dot-config
crond=/usr/sbin/crond

start() {
    echo -n "Starting cron daemon: "

    if [ -f $dotconfig ]; then
	. $dotconfig
    else
	echo "$0 unable to source dot-config ($dotconfig)!"
    fi

    WRS_LOG=$WRS_LOG_OTHER

    # if empty turn it to /dev/null
    if [ -z $WRS_LOG ]; then
	WRS_LOG="/dev/null";
    fi
    # if a pathname, use it
    if echo "$WRS_LOG" | grep / > /dev/null; then
		eval LOGPIPE=\" \> $WRS_LOG 2\>\&1 \";
    elif [ "$WRS_LOG" == "default_syslog" ]; then
		# not a pathname: use verbatim
		eval LOGPIPE=\" 2\>\&1 \| logger -t crond --prio-prefix\"
    else
		# not a pathname: use verbatim
		eval LOGPIPE=\" 2\>\&1 \| logger -t crond -p $WRS_LOG\"
    fi

    # set msg level
    if [ ! -z $CONFIG_WRS_LOG_LEVEL_OTHER ]; then
		WRS_MSG_LEVEL=$CONFIG_WRS_LOG_LEVEL_OTHER
		export WRS_MSG_LEVEL
    fi

    # be carefull with pidof, no running script should have the same name as
    # process
    if pidof crond > /dev/null; then
		# crond already running
		echo "Failed (already running?)"
    else
		eval $crond -S -b -c /etc/cron.d $LOGPIPE
		echo "OK"
    fi
}

stop() {
    echo -n "Stopping crond: "
    start-stop-daemon -K -q --exec $crond 
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

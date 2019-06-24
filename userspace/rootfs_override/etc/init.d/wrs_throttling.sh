#!/bin/sh

tmpdir=/tmp
wrs_throttling_set_status_file="$tmpdir"/wrs_throttling_set_status

# this script shall be called before enabling the switching
dotconfig=/wr/etc/dot-config

set -o pipefail

start() {
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
	eval LOGPIPE=\" 2\>\&1 \| logger -t throttling --prio-prefix\"
    else
	# not a pathname: use verbatim
	eval LOGPIPE=\" 2\>\&1 \| logger -t throttling -p $WRS_LOG\"
    fi

    echo -n "Starting wrs_throttling: "
    if [ "$CONFIG_NIC_THROTTLING_ENABLED" = "y" ] \
      && [ ! -z "$CONFIG_NIC_THROTTLING_VAL" ] ; then
	eval /wr/bin/wrs_throttling -t "$CONFIG_NIC_THROTTLING_VAL" $LOGPIPE
	ret=$?
	if [ $ret -eq 0 ]; then
	    echo "OK"
	    echo "ok" > $wrs_throttling_set_status_file
	else
	    echo "Failed"
	    echo "failed" > $wrs_throttling_set_status_file
	fi
    else
	echo "Disabled"
	echo "disabled" > $wrs_throttling_set_status_file
    fi

}

stop() {
    echo -n "Stopping wrs_throttling: "
    eval /wr/bin/wrs_throttling -d $LOGPIPE
    ret=$?
    if [ $ret -eq 0 ]; then
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

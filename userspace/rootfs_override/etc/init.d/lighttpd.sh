#!/bin/ash
#
# Starts lighttpd daemon.
#
MONIT=/usr/bin/monit

dotconfig=/wr/etc/dot-config

start_counter() {
	# increase boot counter
	COUNTER_FILE="/tmp/start_cnt_httpd"
	START_COUNTER=1
	if [ -f "$COUNTER_FILE" ];
	then
	    read -r START_COUNTER < $COUNTER_FILE
	    START_COUNTER=$((START_COUNTER+1))
	fi
	echo "$START_COUNTER" > $COUNTER_FILE
}

start() {
	echo -n "Starting lighttpd daemon: "

	if [ -f $dotconfig ]; then
		. $dotconfig
	else
		echo "$0 unable to source dot-config ($dotconfig)!"
	fi

	if [ "$CONFIG_HTTPD_DISABLE" = "y" ]; then
		echo "web interface disabled in dot-config!"
		if [ "$1" != "force" ]; then
			# Unmonitor web server (lighttpd), ignore all printouts
			# from monit.
			# Run in background since monit may wait for a timeout.
			$MONIT unmonitor lighttpd &>/dev/null &
			exit 0
		fi
		echo -n "Force start of lighttpd: "
	fi

	# note start-stop-daemon does not create pidfile, only check if pid is
	# running
	start-stop-daemon -q -p /var/run/lighttpd.pid -S \
		--exec /usr/sbin/lighttpd -- -f /etc/lighttpd/lighttpd.conf
	ret=$?
	if [ $ret -eq 0 ]; then
		start_counter
		echo "OK"
	elif [ $ret -eq 1 ]; then
		echo "Failed (already running?)"
	else
		echo "Failed"
	fi

	# check whether the process was monitored
	$MONIT summary 2>&1 | grep lighttpd | grep "Not monitored" &> /dev/null
	if [ $? -eq 0 ]; then
		echo "web interface was not monitored, enabling monitoring"
		# the process was not monitored, enable monitoring
		# this will generate extra log entries from monit
		$MONIT monitor lighttpd
	fi
}

stop() {
	echo -n "Stopping lighttpd daemon: "
	start-stop-daemon -K -q -p /var/run/lighttpd.pid
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
	echo "Usage: $0 {start <force>|stop|restart}"
	echo "    start force -- enable web interface even it is disabled in the dot-config"
	exit 1
esac

exit $?

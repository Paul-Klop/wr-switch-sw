#!/bin/sh
#
# Controls lldpd.
#

CONFIG=/etc/lldpd.conf

case $1 in
    start)
	
	printf "Creating lldpd config: "
	echo "configure system hostname '$(hostname)'" > $CONFIG
	echo "configure system description  'WR-SWITCH: $(/wr/bin/wrsw_version)'" >> $CONFIG
	echo "resume" >> $CONFIG

	printf "Starting lldpd: "
	start-stop-daemon -S -q -p /var/run/lldpd.pid --exec /usr/sbin/lldpd
	[ $? = 0 ] && echo "OK" || echo "FAIL"
	;;
    stop)
	printf "Stopping lldpd: "
	start-stop-daemon -K -q -p /var/run/lldpd.pid
	[ $? = 0 ] && echo "OK" || echo "FAIL"
	;;
    restart)
	$0 stop
	$0 start
	;;
    *)
	echo "Usage: $0 {start|stop|restart}"
	exit 1
	;;
esac

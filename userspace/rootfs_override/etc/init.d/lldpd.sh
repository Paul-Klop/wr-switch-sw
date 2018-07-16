#!/bin/sh
#
# Controls lldpd.
#

LLDPD_CONFIG=/etc/lldpd.conf
LLDPD=/usr/sbin/lldpd
# -x -- Enable SNMP subagent.
LLDPD_OPT=-x

dotconfig=/wr/etc/dot-config


start_counter() {
    # increase start counter
    COUNTER_FILE="/tmp/start_cnt_lldpd"
    START_COUNTER=1
    if [ -f "$COUNTER_FILE" ];
    then
	read -r START_COUNTER < $COUNTER_FILE
	START_COUNTER=$((START_COUNTER+1))
    fi
    echo "$START_COUNTER" > $COUNTER_FILE
}

start() {

    echo -n "Creating lldpd config: "
    echo "configure system hostname '$(hostname)'" > $LLDPD_CONFIG
    echo "configure system description  'WR-SWITCH: $(/wr/bin/wrsw_version)'" >> $LLDPD_CONFIG
    echo "resume" >> $LLDPD_CONFIG

    echo -n "Starting lldpd: "
#     if [ -f $dotconfig ]; then
# 	. $dotconfig
#     else
# 	echo "$0 unable to source dot-config ($dotconfig)!"
#     fi
# 

    start-stop-daemon -S -q -p /var/run/lldpd.pid --exec $LLDPD -- $LLDPD_OPT
    ret=$?
    if [ $ret -eq 0 ]; then
	start_counter
	echo "OK"
    elif [ $ret -eq 1 ]; then
	echo "Failed (already running?)"
    else
	echo "Failed"
    fi
}

stop() {
    echo -n "Stopping lldpd: "
    start-stop-daemon -K -q -p /var/run/lldpd.pid
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

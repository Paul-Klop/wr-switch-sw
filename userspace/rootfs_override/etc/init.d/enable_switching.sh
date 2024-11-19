#!/bin/sh

dotconfig=/wr/etc/dot-config

start() {
    echo -n "Enable switching: "

    if [ -f $dotconfig ]; then
        . $dotconfig
    else
        echo -n "$0 unable to source dot-config ($dotconfig)! "
    fi

    # bring up all interfaces
    for i in `ls /sys/class/net | grep wri`
    do
	# If port is configured to be down after boot, don't bring it up
	i_port=$(echo $i | cut -c4-)
	i_port_zero=$(printf "%02d" $i_port)
	keep_down=$(eval "echo \$CONFIG_PORT"$i_port_zero"_IFACE_DOWN_AFTER_BOOT")
	if [ "$keep_down" != "y" ]; then
	    ifconfig $i up
	fi
    done

    echo "OK"
}

stop() {
    echo -n "Disable switching: "

    # bring down all interfaces
    for i in `ls /sys/class/net | grep wri`
    do
	    ifconfig $i down
    done

    echo "OK"
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

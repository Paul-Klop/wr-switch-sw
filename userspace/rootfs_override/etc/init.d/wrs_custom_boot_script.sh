#!/bin/sh

set -o pipefail

dotconfig=/wr/etc/dot-config
tmpdir=/tmp
tmpscript="$tmpdir"/custom_boot_script.sh
custom_boot_status_file="$tmpdir"/custom_boot_script_status
custom_boot_source_file="$tmpdir"/custom_boot_script_source
custom_boot_source_url_file="$tmpdir"/custom_boot_script_url

print_error() {
    echo "$1"
    eval echo "$0: $1" $LOGPIPE
}
start() {
    echo -n "Executing custom boot script: "

    if [ -f $dotconfig ]; then
	. $dotconfig
    else
	echo "$0 unable to source dot-config ($dotconfig)!"
    fi

    # set log destination
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
	eval LOGPIPE=\" 2\>\&1 \| logger -t custom_boot_script --prio-prefix\"
    else
	# not a pathname: use verbatim
	eval LOGPIPE=\" 2\>\&1 \| logger -t custom_boot_script -p $WRS_LOG\"
    fi

    # set msg level
    if [ ! -z $CONFIG_WRS_LOG_LEVEL_OTHER ]; then
	WRS_MSG_LEVEL=$CONFIG_WRS_LOG_LEVEL_OTHER
	export WRS_MSG_LEVEL
    fi

    # If custom boot script is not enabled, exit
    if [ ! "$CONFIG_CUSTOM_BOOT_SCRIPT_ENABLED" = "y" ]; then
	echo "disabled"
	echo "disabled" > "$custom_boot_status_file"
	exit;
    fi

    if [ "$CONFIG_CUSTOM_BOOT_SCRIPT_SOURCE_LOCAL" = "y" ]; then
	echo "local" > "$custom_boot_source_file"
	execute_script=/wr/bin/custom_boot_script.sh
    fi
    if [ "$CONFIG_CUSTOM_BOOT_SCRIPT_SOURCE_REMOTE" = "y" ]; then
	echo "remote" > "$custom_boot_source_file"

	# replace IPADDR and MACADDR, to have a device-specific name
	macaddr=$(cat /sys/class/net/eth0/address)
	ipaddr=$(ifconfig eth0 | grep inet | cut -d: -f 2 | cut '-d '  -f 1)
	if [ -z "$ipaddr" ]; then
	    eval echo "$0: Warning no IP set!" $LOGPIPE
	fi
	host_name=`hostname`
	URL=$(echo $CONFIG_CUSTOM_BOOT_SCRIPT_SOURCE_REMOTE_URL | \
	    sed -e s/MACADDR/$macaddr/g -e s/IPADDR/$ipaddr/g -e s/HOSTNAME/$host_name/g)
	# split the parts, as we need to handle tftp by hand
	proto=$(echo $URL | cut -d: -f 1)
	host=$(echo $URL | cut -d/ -f 3)
	filename=$(echo $URL | cut -d/ -f 4-)

	 # save URL of custom boot script, to be used by SNMPd
	echo "$URL" > "$custom_boot_source_url_file"
	rm -f "$tmpscript"
	case $proto in
	    http|ftp)
		wget $URL -O "$tmpscript"
		;;
	    tftp)
		tftp -g -r "$filename" -l "$tmpscript" "$host"
		;;
	    *)
		echo "Invalid URL for custom boot script: \"$URL\"" >& 2
		;;
	esac
	if [ ! -f "$tmpscript" ]; then
	    print_error "Download error!"
	    echo "download_error" > "$custom_boot_status_file"
	    exit
	fi
	execute_script="$tmpscript"
	chmod +x "$execute_script"
    fi

    if [ -z "$execute_script" ]; then
	print_error "Please specify local or remote in dot-config!"
	echo "wrong_source" > "$custom_boot_source_file"
	echo "wrong_source" > "$custom_boot_status_file"
	exit
    fi

    if [ ! -f "$execute_script" ]; then
	print_error "$execute_script not found!"
	exit
    fi
    # redirect output of script to OTHER_LOG
    eval "$execute_script" start $LOGPIPE
    ret=$?
    if [ $ret -eq 0 ]; then
	echo "OK"
	echo "ok" > "$custom_boot_status_file"
    else
	echo "Failed"
	echo "failed" > "$custom_boot_status_file"
    fi
}

stop() {
    if [ -f $dotconfig ]; then
	. $dotconfig
    else
	echo "$0 unable to source dot-config ($dotconfig)!"
    fi

    # set log destination
    WRS_LOG=$CONFIG_WRS_LOG_OTHER

    # if empty turn it to /dev/null
    if [ -z $WRS_LOG ]; then
	WRS_LOG="/dev/null";
    fi

    # if a pathname, use it
    if echo "$WRS_LOG" | grep / > /dev/null; then
	eval LOGPIPE=\" \> $WRS_LOG 2\>\&1 \";
    elif "$WRS_LOG" = "default_syslog"; then
	# not a pathname: use verbatim
	eval LOGPIPE=\" 2\>\&1 \| logger -t custom_boot_script --prio-prefix\"
    else
	# not a pathname: use verbatim
	eval LOGPIPE=\" 2\>\&1 \| logger -t custom_boot_script -p $WRS_LOG\"
    fi

    if [ ! "$CONFIG_CUSTOM_BOOT_SCRIPT_ENABLED" = "y" ]; then
	exit;
    fi
    echo -n "Stopping custom boot script: "
    if [ "$CONFIG_CUSTOM_BOOT_SCRIPT_SOURCE_LOCAL" = "y" ]; then
	execute_script=/wr/bin/custom_boot_script.sh
    fi
    if [ "$CONFIG_CUSTOM_BOOT_SCRIPT_SOURCE_REMOTE" = "y" ]; then
	execute_script="$tmpscript"
    fi

    if [ -z "$execute_script" ]; then
	print_error "Please specify local or remote in dot-config!"
	exit
    fi

    if [ ! -f "$execute_script" ]; then
	print_error "$execute_script not found!"
	exit
    fi

    # redirect output of script to OTHER_LOG
    eval "$execute_script" stop $LOGPIPE
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
	echo "Usage: $0 {start|stop|restart}"
	exit 1
	;;
esac

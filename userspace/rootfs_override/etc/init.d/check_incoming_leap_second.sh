#!/bin/sh

dotconfig=/wr/etc/dot-config

pBaseName=leapsec
pFullName=wrs_$pBaseName
process=/wr/bin/$pFullName

echo -n "Starting $process  "

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
else
	# not a pathname: use verbatim
	if [ "$WRS_LOG" = "default_syslog" ]; then
		eval LOGPIPE=\" 2\>\&1 \| logger -t $pBaseName --prio-prefix\"
    else
		eval LOGPIPE=\" 2\>\&1 \| logger -t $pBaseName -p $WRS_LOG\"
	fi
fi

# set msg level
if [ ! -z $CONFIG_WRS_LOG_LEVEL_OTHER ]; then
	WRS_MSG_LEVEL=$CONFIG_WRS_LOG_LEVEL_OTHER
	export WRS_MSG_LEVEL
fi

# be carefull with pidof, no running script should have the same name as
# process
if pidof $pFullName  > /dev/null; then
	# Process s already running
	eval echo "Failed (already running?)" $LOGPIPE
else
	eval $process $LOGPIPE \&
	echo "OK"
fi


#!/bin/sh
#
# Starts sshd.
#

dotconfig=/wr/etc/dot-config
permanent_path_ssh=/update/permanent/ssh

# Make sure the ssh-keygen progam exists
[ -f /usr/bin/ssh-keygen ] || exit 0

start_counter() {
	# increase boot counter
	COUNTER_FILE="/tmp/start_cnt_sshd"
	START_COUNTER=1
	if [ -f "$COUNTER_FILE" ];
	then
		read -r START_COUNTER < $COUNTER_FILE
		START_COUNTER=$((START_COUNTER+1))
	fi
	echo "$START_COUNTER" > $COUNTER_FILE
}

start() {
	echo -n "Starting sshd: "

	if [ -f "$dotconfig" ]; then
		. "$dotconfig"
	else
		echo "$0 unable to source dot-config ($dotconfig)!"
	fi

 	# copy authorized keys if exists
	if [ -f /usr/authorized_keys ] ; then
		mkdir -p /root/.ssh/
		cp /usr/authorized_keys /root/.ssh/
	fi

	# Make sure ssh directory exists
	mkdir -p /etc/ssh

	# Copy keys from permanent location if available
	if [ -d "$permanent_path_ssh" ] ; then
		cp "$permanent_path_ssh"/ssh_host_*_key* /etc/ssh/
	fi

	mkdir -p "$permanent_path_ssh"

	# Check for the ssh keys
	if     [ ! -f /etc/ssh/ssh_host_rsa_key ] \
        || [ ! -f /etc/ssh/ssh_host_dsa_key ] \
        || [ ! -f /etc/ssh/ssh_host_ecdsa_key ] \
        || [ ! -f /etc/ssh/ssh_host_ed25519_key ]; then
# 		echo -n "generating ssh keys... "
		/usr/bin/ssh-keygen -A
		cp /etc/ssh/ssh_host_*_key* "$permanent_path_ssh"
	fi

	if [ "$CONFIG_ROOT_ACCESS_DISABLE" = "y" ]; then
		sed -i "s|^PermitRootLogin.*|PermitRootLogin prohibit-password # replaced by $0|g" /etc/ssh/sshd_config
	else
		sed -i "s|^PermitRootLogin.*|PermitRootLogin yes # replaced by $0|g" /etc/ssh/sshd_config
	fi

	umask 077
	# /var/run/sshd.pid is created automatically by sshd
	start-stop-daemon -S -q -p /var/run/sshd.pid --exec /usr/sbin/sshd
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
	echo -n "Stopping sshd: "
	start-stop-daemon -K -q -p /var/run/sshd.pid
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
esac

exit $?


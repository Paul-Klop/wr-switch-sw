#!/bin/sh
#
# Starts nslcd needed for LDAP
#
MONIT=/usr/bin/monit
dotconfig=/wr/etc/dot-config

start_counter() {
	# increase boot counter
	COUNTER_FILE="/tmp/start_cnt_ldap"
	START_COUNTER=1
	if [ -f "$COUNTER_FILE" ];
	then
		read -r START_COUNTER < $COUNTER_FILE
		START_COUNTER=$((START_COUNTER+1))
	fi
	echo "$START_COUNTER" > $COUNTER_FILE
}

start() {
	echo -n "Starting nslcd (LDAP): "
	
	if [ -f "$dotconfig" ]; then
		. "$dotconfig"
	else
		echo "$0 unable to source dot-config ($dotconfig)!"
	fi
	
	if [ "$CONFIG_LDAP_ENABLE" != "y" ]; then
		echo "LDAP not enabled in dot-config"
		# Unmonitor web server (nslcd), ignore all printouts
		# from monit.
		# Run in background since monit may wait for a timeout.
		$MONIT unmonitor nslcd &>/dev/null &
		exit 0
	fi
	
	if [ -z "$CONFIG_LDAP_SERVER" ]; then
		echo "Failed! LDAP server not defined"
		exit 0
	fi
	# fill LDAP server address
	cp -a /usr/etc/nslcd.conf /etc/nslcd.conf
	sed -i "s,^uri CONFIG_LDAP_SERVER_ADDRESS,uri $CONFIG_LDAP_SERVER,g" /etc/nslcd.conf

	if [ -z "$CONFIG_LDAP_SEARCH_BASE" ]; then
		echo "Failed! LDAP search base not defined"
		exit 0
	fi
	# fill LDAP search base
	sed -i "s/CONFIG_LDAP_SEARCH_BASE/$CONFIG_LDAP_SEARCH_BASE/g" /etc/nslcd.conf

	if [ "$CONFIG_LDAP_FILTER_NONE" = "y" ]; then
		# no filter
		sed -i "s/CONFIG_LDAP_FILTER//g" /etc/nslcd.conf
	elif [ "$CONFIG_LDAP_FILTER_EGROUP" = "y" ]; then
		if [ -z "$CONFIG_LDAP_FILTER_EGROUP_STR" ]; then
			echo -n "Warning: CONFIG_LDAP_FILTER_EGROUP_STR empty! "
		fi
		sed -i "s/CONFIG_LDAP_FILTER/(memberOf=CN=$CONFIG_LDAP_FILTER_EGROUP_STR,OU=e-groups,OU=Workgroups,$CONFIG_LDAP_SEARCH_BASE)/g" /etc/nslcd.conf
	elif [ "$CONFIG_LDAP_FILTER_CUSTOM" = "y" ]; then
		if [ -z "$CONFIG_LDAP_FILTER_CUSTOM_STR" ]; then
			echo -n "Warning: CONFIG_LDAP_FILTER_CUSTOM_STR empty! "
		fi
		sed -i "s/CONFIG_LDAP_FILTER/$CONFIG_LDAP_FILTER_CUSTOM_STR/g" /etc/nslcd.conf
	fi

	# add ldap to /etc/nsswitch.conf
	cp -a /usr/etc/nsswitch.conf /etc/nsswitch.conf
	sed -i "s/^passwd:[ \t]*files\$/passwd: files ldap/g" /etc/nsswitch.conf
	sed -i "s/^group:[ \t]*files\$/group: files ldap/g" /etc/nsswitch.conf
	sed -i "s/^shadow:[ \t]*files\$/shadow: files ldap/g" /etc/nsswitch.conf

	cp -a /usr/etc/pam.d/sshd /etc/pam.d/sshd
	if [ "$CONFIG_AUTH_KRB5" = "y" ]; then
		if [ -z "$CONFIG_AUTH_KRB5_SERVER" ]; then
			echo "Failed! CONFIG_AUTH_KRB5_SERVER empty!"
			exit 0
		fi
		
		sed -i "s,# auth line to be replaced by startup scripts,# added by $0\nauth       sufficient   /lib/security/pam_krb5.so minimum_uid=1000\n,g" /etc/pam.d/sshd
		sed -i "s,# account line to be replaced by startup scripts,# added by $0\naccount    required     /lib/security/pam_krb5.so minimum_uid=1000\n,g" /etc/pam.d/sshd
		sed -i "s,# session line to be replaced by startup scripts,# added by $0\nsession    required     /lib/security/pam_krb5.so minimum_uid=1000\n,g" /etc/pam.d/sshd
		cp -a /usr/etc/krb5.conf /etc/krb5.conf
		sed -i "s,default_realm = CONFIG_AUTH_KRB5_SERVER,default_realm = $CONFIG_AUTH_KRB5_SERVER,g" /etc/krb5.conf
	fi
	
	if [ "$CONFIG_AUTH_LDAP" = "y" ]; then
		sed -i "s,# auth line to be replaced by startup scripts,# added by $0\nauth       sufficient   /lib/security/pam_ldap.so minimum_uid=1000\n,g" /etc/pam.d/sshd
		sed -i "s,# account line to be replaced by startup scripts,# added by $0\naccount    required     /lib/security/pam_ldap.so minimum_uid=1000\n,g" /etc/pam.d/sshd
		sed -i "s,# session line to be replaced by startup scripts,# added by $0\nsession    required     /lib/security/pam_ldap.so minimum_uid=1000\n,g" /etc/pam.d/sshd

	fi

	# /var/run/nslcd/nslcd.pid is created automatically by nslcd
	start-stop-daemon -S -q -p /var/run/nslcd/nslcd.pid --exec /usr/sbin/nslcd
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
	echo -n "Stopping nslcd (LDAP): "
	start-stop-daemon -K -q -p /var/run/nslcd/nslcd.pid
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


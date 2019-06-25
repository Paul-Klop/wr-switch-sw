#!/bin/sh

# Adam Wujek @ CERN
#
# This script is to be run by udhcpc
#
# udhcpc should get hostname the from DHCP server.
#
# check whether we got hostname from DHCP server
log_output=/dev/kmsg

exec 1>$log_output

if [ -n "$hostname" ]; then
    /bin/hostname "$hostname"
    echo "$hostname" > /etc/hostname
fi

# source default script
. /usr/share/udhcpc/default.script

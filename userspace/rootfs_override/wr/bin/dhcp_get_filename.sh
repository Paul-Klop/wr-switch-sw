#!/bin/sh

# Adam Wujek @ CERN
#
# This script is to be run by udhcpc
#
# udhcpc should save URL to the dot-config in boot_file.
# URL shall be stored in "filename" configuration field of DHCP server.
#
# We create a temporary file in /tmp, to avoid wearing flash if not
# needed. Then we replace the real file if different.
tmpdir="/tmp"

# check whether we got path to the dot-config file from DHCP server
if [ -n "$boot_file" ]; then
    # check whether the path consists potocol type
    proto=$(echo "$boot_file" | cut -d: -f 1)
    if [ "$proto" != "tftp" ] && [ "$proto" != "ftp" ] && [ "$proto" != "http" ] && [ -n "$siaddr" ]; then
	# There is no protocol type in the path. If $siaddr is not empty, assume
	# config should come from $siaddr/$boot_file via tftp
	boot_file=tftp://"$siaddr"/"$boot_file"
    fi
    # Path with protocol type (added above or originally included) that was
    # received from the DHCP server.
    echo $boot_file > "$tmpdir"/dot-config_source_url
fi

# save IP@ of the boot server
if [ -n "$siaddr" ]; then
   echo $siaddr > "$tmpdir"/boot_server_ip
fi

# Set hostname
if [ -n "$hostname" ]; then
    /bin/hostname "$hostname"
    echo "$hostname" > /etc/hostname
fi


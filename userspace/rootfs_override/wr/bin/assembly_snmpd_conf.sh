#!/bin/sh

# Author: Adam Wujek for GSI
# Copyright: 2021 GSI


# Assempbly sysObjectID value
MANUFACTURER=`/wr/bin/wrs_version -t | grep manufacturer | cut -d " " -f 2-`
SCB=`/wr/bin/wrs_version -t | grep scb-version | cut -d " " -f 2`
SCB_MAJOR=`echo $SCB | cut -d "." -f 1`
SCB_MINOR=`echo $SCB | cut -d "." -f 2`

case ${SCB_MAJOR} in
    3)
	SCB_MAJOR_N=${SCB_MAJOR}
	;;
    *)
	# set as unknown (1)
	# Output sysObjectID value to configuration file
	echo "sysObjectID .1.3.6.1.4.1.96.100.1000.1.1" >> $1
	exit 0;
	;;
esac

case ${MANUFACTURER} in
    "Seven Solutions" | "7S" | "SAFRAN")
	MANUFACTURER_N=2
	;;

    "Creotech Ins SA")
	MANUFACTURER_N=3
	;;

    "SyncTechnology")
	MANUFACTURER_N=4
	;;

    "OPNT" | "OPNT           ")
	MANUFACTURER_N=5
	;;

	*)
	# set as unknown (1)
	MANUFACTURER_N=1
	;;
esac

case ${SCB_MINOR} in
    3|4|5)
	SCB_MINOR_N=${SCB_MINOR}
	;;
    *)
	# set as unknown (1)
	SCB_MINOR_N=1
	;;
esac

# Output sysObjectID value to configuration file
echo "sysObjectID .1.3.6.1.4.1.96.100.1000.${SCB_MAJOR_N}.${MANUFACTURER_N}.${SCB_MINOR_N}" >> $1

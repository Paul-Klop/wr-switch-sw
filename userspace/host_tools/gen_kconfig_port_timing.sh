#!/bin/bash

# Jean-Claude BAU @CERN
# script to generate Kconfig timing port configuration.
#
# Parameters:
#     -o file Overwrite the default output file name
#

OUTPUT_FILE="Kconfig_port_timing.in"

script_name="$0"

#decode script parameters
while getopts o: option
do
	case "${option}" in
		"o") OUTPUT_FILE=${OPTARG};;
	esac
done

function print_header() { 
	true
}

function print_footer() {
	true
}

function print_port_header() { 
	local portIdx=$1
	local portStr=`printf "%02d" $portIdx`
	
	echo -e "\nmenu \"PORT ${portIdx}\"" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${portStr}_IFACE" >>$OUTPUT_FILE
	echo -e "\tstring \"Network interface\"" >>$OUTPUT_FILE
	echo -e "\tdefault \"wri${portIdx}\"" >>$OUTPUT_FILE
	echo -e "\thelp" >>$OUTPUT_FILE
	echo -e "\t  Used to set the physical port interface name: \"wri[1-18]\"" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${portStr}_IFACE_DOWN_AFTER_BOOT" >>$OUTPUT_FILE
	echo -e "\tbool \"Keep interface down after startup\"" >>$OUTPUT_FILE
	echo -e "\thelp" >>$OUTPUT_FILE
	echo -e "\t  Don't bring up the network interface after startup." >>$OUTPUT_FILE
	echo -e "\t  The interface can be brought up later by e.g. ifconfig command." >>$OUTPUT_FILE

	echo -e "\nconfig PORT${portStr}_FIBER" >>$OUTPUT_FILE
	echo -e "\tint  \"Fiber type\"" >>$OUTPUT_FILE
	echo -e "\tdefault 0" >>$OUTPUT_FILE
	echo -e "\thelp" >>$OUTPUT_FILE
	echo -e "\t  Used to set the type of fiber (number referring to the corresponding " >>$OUTPUT_FILE
	echo -e "\t  FIBERXX_PARAMS)" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${portStr}_CONSTANT_ASYMMETRY" >>$OUTPUT_FILE
	echo -e "\tint \"asymmetryCorrectionPortDS.constantAsymmetry\"" >>$OUTPUT_FILE
	echo -e "\tdefault 0" >>$OUTPUT_FILE
	echo -e "\thelp" >>$OUTPUT_FILE
	echo -e "\t   Used to set the constant delay asymmetry." >>$OUTPUT_FILE

	echo -e "\nchoice" >>$OUTPUT_FILE
	echo -e "\tprompt \"Number of port instances\"" >>$OUTPUT_FILE
	echo -e "\tdefault PORT${portStr}_INSTANCE_COUNT_1" >>$OUTPUT_FILE
	for inst in `seq 0 $instCount` ; do
		echo -e "\tconfig PORT${portStr}_INSTANCE_COUNT_$inst" >>$OUTPUT_FILE
		echo -e "\t  bool \"$inst\"" >>$OUTPUT_FILE
	done
	echo -e "endchoice" >>$OUTPUT_FILE

} 

function print_port_footer() { 
 
	echo -e "\nendmenu" >>$OUTPUT_FILE
 
} 

function print_instance_header() { 
	local portIdx=$1
	local portStr=`printf "%02d" $portIdx`
	
	local instIdx=$2
	local instStr=`printf "%02d" $instIdx`
	
	if [ $instIdx -eq 1 ] ; then prof=WR ; else prof=HA ; fi
	local tx=${port_tx[$1]}
	local rx=${port_rx[$1]}
	local t24p=${port_t24p[$1]}
	echo -e "\nmenu \"Instance ${instIdx}\"" >>$OUTPUT_FILE
	echo -n "	depends on " >>$OUTPUT_FILE
	for inst in `seq $instIdx $instCount` ; do
		if [ $inst != $instIdx ] ; then echo  -n " ||  " >>$OUTPUT_FILE; fi
		echo -n "PORT${portStr}_INSTANCE_COUNT_$inst" >>$OUTPUT_FILE
	done
	echo -e >>$OUTPUT_FILE

	echo -e "\nchoice" >>$OUTPUT_FILE
	echo -e "    prompt \"Network protocol\"" >>$OUTPUT_FILE
	echo -e "    default PORT${portStr}_INST${instStr}_PROTOCOL_RAW" >>$OUTPUT_FILE
	echo -e "    config PORT${portStr}_INST${instStr}_PROTOCOL_RAW" >>$OUTPUT_FILE
	echo -e "        bool \"IEEE 802.3\"" >>$OUTPUT_FILE
	echo -e "    config PORT${portStr}_INST${instStr}_PROTOCOL_UDP_IPV4" >>$OUTPUT_FILE
	echo -e "        bool \"UDP/Ipv4\"" >>$OUTPUT_FILE
	echo -e "endchoice" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${portStr}_INST${instStr}_IFACE_IP_ADDR" >>$OUTPUT_FILE
	echo -e "	string \"IP address\"" >>$OUTPUT_FILE
	echo -e "	depends on PORT${portStr}_INST${instStr}_PROTOCOL_UDP_IPV4" >>$OUTPUT_FILE
	echo -e "	default \"192.168.100.${portIdx}\"" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  IP to be set on an interface assigned to this instance" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${portStr}_INST${instStr}_IFACE_IP_MASK" >>$OUTPUT_FILE
	echo -e "	string \"Mask\"" >>$OUTPUT_FILE
	echo -e "	depends on PORT${portStr}_INST${instStr}_PROTOCOL_UDP_IPV4" >>$OUTPUT_FILE
	echo -e "	default \"255.255.255.0\"" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  IP mask to be set on an interface assigned to this instance" >>$OUTPUT_FILE

	echo -e "\nchoice" >>$OUTPUT_FILE
	echo -e "    prompt \"Delay mechanism\"" >>$OUTPUT_FILE
	echo -e "    default PORT${portStr}_INST${instStr}_MECHANISM_E2E" >>$OUTPUT_FILE
	echo -e "    config PORT${portStr}_INST${instStr}_MECHANISM_E2E" >>$OUTPUT_FILE
	echo -e "        bool \"End-to-end\"" >>$OUTPUT_FILE
	echo -e "    config PORT${portStr}_INST${instStr}_MECHANISM_P2P" >>$OUTPUT_FILE
	echo -e "        bool \"Peer-to-peer\"" >>$OUTPUT_FILE
	echo -e "endchoice" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${portStr}_INST${instStr}_MONITOR" >>$OUTPUT_FILE
	echo -e "	bool \"SNMP monitoring\"" >>$OUTPUT_FILE
	echo -e "	default y" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  Option to disable or enable triggering errors in SNMP on a port" >>$OUTPUT_FILE
	
	echo -e "\nchoice" >>$OUTPUT_FILE
	echo -e "	prompt \"PTP Profile\"" >>$OUTPUT_FILE
	echo -e "	default PORT${portStr}_INST${instStr}_PROFILE_KEEP_GLOBAL" >>$OUTPUT_FILE
	echo -e "	config PORT${portStr}_INST${instStr}_PROFILE_KEEP_GLOBAL" >>$OUTPUT_FILE
	echo -e "		bool \"Keep global\"" >>$OUTPUT_FILE
	echo -e "	config PORT${portStr}_INST${instStr}_PROFILE_PTP" >>$OUTPUT_FILE
	echo -e "		bool \"Default (IEEE 1588)\"" >>$OUTPUT_FILE
	echo -e "	config PORT${portStr}_INST${instStr}_PROFILE_HA_WR" >>$OUTPUT_FILE
	echo -e "		bool \"White Rabbit / High-Accuracy (IEEE 1588)\"" >>$OUTPUT_FILE
	echo -e "	config PORT${portStr}_INST${instStr}_PROFILE_CUSTOM" >>$OUTPUT_FILE
	echo -e "		bool \"Custom\"" >>$OUTPUT_FILE
	echo -e "endchoice" >>$OUTPUT_FILE

	echo -e "\nchoice" >>$OUTPUT_FILE
	echo -e "	prompt \"Extensions configuration\"" >>$OUTPUT_FILE
	echo -e "	depends on \\" >>$OUTPUT_FILE
	echo -e "		(PORT${portStr}_INST${instStr}_PROFILE_KEEP_GLOBAL \\" >>$OUTPUT_FILE
	echo -e "			&& (GLOBAL_PROFILE_HA_WR \\" >>$OUTPUT_FILE
	echo -e "			    || GLOBAL_PROFILE_CUSTOM) \\" >>$OUTPUT_FILE
	echo -e "		) \\" >>$OUTPUT_FILE
	echo -e "		|| PORT${portStr}_INST${instStr}_PROFILE_HA_WR \\" >>$OUTPUT_FILE
	echo -e "		|| PORT${portStr}_INST${instStr}_PROFILE_CUSTOM" >>$OUTPUT_FILE
	echo -e "	default PORT${portStr}_INST${instStr}_EXTENSION_NONE if (PORT${portStr}_INST${instStr}_PROFILE_KEEP_GLOBAL && GLOBAL_PROFILE_PTP) || PORT${portStr}_INST${instStr}_PROFILE_PTP" >>$OUTPUT_FILE
	echo -e "	default PORT${portStr}_INST${instStr}_EXTENSION_WR if (PORT${portStr}_INST${instStr}_PROFILE_KEEP_GLOBAL && GLOBAL_PROFILE_HA_WR) || PORT${portStr}_INST${instStr}_PROFILE_HA_WR" >>$OUTPUT_FILE
	echo -e "	config PORT${portStr}_INST${instStr}_EXTENSION_NONE" >>$OUTPUT_FILE
	echo -e "		bool \"None\"" >>$OUTPUT_FILE
	echo -e "		help" >>$OUTPUT_FILE
	echo -e "		  Don't use extensions." >>$OUTPUT_FILE
	echo -e "	config PORT${portStr}_INST${instStr}_EXTENSION_WR" >>$OUTPUT_FILE
	echo -e "		bool \"WR only\"" >>$OUTPUT_FILE
	echo -e "		help" >>$OUTPUT_FILE
	echo -e "		  Use White Rabbit extension." >>$OUTPUT_FILE
	echo -e "	config PORT${portStr}_INST${instStr}_EXTENSION_L1S" >>$OUTPUT_FILE
	echo -e "		bool \"HA/L1Sync only\"" >>$OUTPUT_FILE
	echo -e "		help" >>$OUTPUT_FILE
	echo -e "		  Use HA/L1Sync extension." >>$OUTPUT_FILE
	echo -e "	config PORT${portStr}_INST${instStr}_EXTENSION_L1S_WR" >>$OUTPUT_FILE
	echo -e "		bool \"HA/L1Sync with WR fallback (autonegotiation)\"" >>$OUTPUT_FILE
	echo -e "		help" >>$OUTPUT_FILE
	echo -e "		  Use HA/L1Sync and White Rabbit extension. HA/L1Sync is" >>$OUTPUT_FILE
	echo -e "		  preffered. If a peer does not support HA/L1Sync this instance" >>$OUTPUT_FILE
	echo -e "		  can use White Rabbit." >>$OUTPUT_FILE
	echo -e "endchoice" >>$OUTPUT_FILE

	echo -e "\nchoice" >>$OUTPUT_FILE
	echo -e "	prompt \"Desired state\"" >>$OUTPUT_FILE
	echo -e "	depends on PTP_OPT_BMCA_EXT_PORT_CONFIG" >>$OUTPUT_FILE
	[[ $portIdx -eq 1 ]] && echo -e "	default PORT${portStr}_INST${instStr}_DESIRADE_STATE_SLAVE if TIME_BC" >>$OUTPUT_FILE
	echo -e "	default PORT${portStr}_INST${instStr}_DESIRADE_STATE_MASTER" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  Define a desied state if External Port Configuration is used (BMCA is" >>$OUTPUT_FILE
	echo -e "	  disabled)." >>$OUTPUT_FILE
	echo -e "	config PORT${portStr}_INST${instStr}_DESIRADE_STATE_MASTER" >>$OUTPUT_FILE
	echo -e "		bool \"Master\"" >>$OUTPUT_FILE
	echo -e "	config PORT${portStr}_INST${instStr}_DESIRADE_STATE_SLAVE" >>$OUTPUT_FILE
	echo -e "		bool \"Slave\"" >>$OUTPUT_FILE
	echo -e "		depends on !TIME_GM" >>$OUTPUT_FILE
	echo -e "	config PORT${portStr}_INST${instStr}_DESIRADE_STATE_PASSIVE" >>$OUTPUT_FILE
	echo -e "		bool \"Passive\"" >>$OUTPUT_FILE
	echo -e "endchoice" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${portStr}_INST${instStr}_BMODE_MASTER_ONLY" >>$OUTPUT_FILE
	echo -e "	bool \"portDS.masterOnly\"" >>$OUTPUT_FILE
	echo -e "	depends on !PTP_OPT_BMCA_EXT_PORT_CONFIG" >>$OUTPUT_FILE
	echo -e "	default y if TIME_GM" >>$OUTPUT_FILE
	echo -e "	default n" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  Use reduced state machine, that can reach only master state." >>$OUTPUT_FILE

	echo -e "\nconfig PORT${portStr}_INST${instStr}_ASYMMETRY_CORRECTION_ENABLE" >>$OUTPUT_FILE
	echo -e "	bool \"asymmetryCorrectionPortDS.enable\"" >>$OUTPUT_FILE
	echo -e "	depends on (PORT${portStr}_INST${instStr}_PROFILE_KEEP_GLOBAL && GLOBAL_PROFILE_PTP) \\" >>$OUTPUT_FILE
	echo -e "		    || (PORT${portStr}_INST${instStr}_PROFILE_KEEP_GLOBAL && GLOBAL_PROFILE_CUSTOM) \\" >>$OUTPUT_FILE
	echo -e "		    || PORT${portStr}_INST${instStr}_PROFILE_PTP \\" >>$OUTPUT_FILE
	echo -e "		    || PORT${portStr}_INST${instStr}_PROFILE_CUSTOM" >>$OUTPUT_FILE
	echo -e "	default y" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  When supported, the value TRUE shall indicate that the mechanism of for the calculation" >>$OUTPUT_FILE
	echo -e "	  of the <delayAsymmetry> for certain media is enabled on the PTP port." >>$OUTPUT_FILE

	echo -e "\nconfig PORT${portStr}_INST${instStr}_EGRESS_LATENCY" >>$OUTPUT_FILE
	echo -e "	int \"timestampCorrectionPortDS.egressLatency (ps)\"" >>$OUTPUT_FILE
	echo -e "	default ${tx}" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  Defines the transmission constant delay (ps)" >>$OUTPUT_FILE
		
	echo -e "\nconfig PORT${portStr}_INST${instStr}_INGRESS_LATENCY" >>$OUTPUT_FILE
	echo -e "	int \"timestampCorrectionPortDS.ingressLatency (ps)\"" >>$OUTPUT_FILE
	echo -e "	default ${rx}" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  Defines the reception constant delay (ps)" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${portStr}_INST${instStr}_T24P_TRANS_POINT" >>$OUTPUT_FILE
	echo -e "	int \"timestampCorrectionPortDS.t24p_trans_point (ps)\"" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  Defines the phase transition point for reception timestamps t2/t4 (ps)" >>$OUTPUT_FILE


	echo -e "\nconfig PORT${portStr}_INST${instStr}_PTP_VERSION_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	bool \"Force PTP version\"" >>$OUTPUT_FILE
	echo -e "	depends on PTP_OPT_PTP_VERSION_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  Force the PTP version used by this instance." >>$OUTPUT_FILE
	echo -e "	  If this option is not set, the PTP version is set based on the used" >>$OUTPUT_FILE
	echo -e "	  extension (not profile!)." >>$OUTPUT_FILE
	echo -e "	  See help of PTP_OPT_PTP_VERSION_OVERWRITE." >>$OUTPUT_FILE

	echo -e "\nchoice" >>$OUTPUT_FILE
	echo -e "	prompt \"PTP version\"" >>$OUTPUT_FILE
	echo -e "	depends on PORT${portStr}_INST${instStr}_PTP_VERSION_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  Define PTP version used by this instance." >>$OUTPUT_FILE
	echo -e "	config PORT${portStr}_INST${instStr}_PTP_VERSION_2_0" >>$OUTPUT_FILE
	echo -e "		bool \"v2.0 (IEEE1588-2008)\"" >>$OUTPUT_FILE
	echo -e "	config PORT${portStr}_INST${instStr}_PTP_VERSION_2_1" >>$OUTPUT_FILE
	echo -e "		bool \"v2.1 (IEEE1588-2019)\"" >>$OUTPUT_FILE
	echo -e "endchoice" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${portStr}_INST${instStr}_ANNOUNCE_INTERVAL_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	bool \"Overwrite default logAnnounceInterval\"" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${portStr}_INST${instStr}_ANNOUNCE_INTERVAL" >>$OUTPUT_FILE
	echo -e "	int \"logAnnounceInterval\" if PORT${portStr}_INST${instStr}_ANNOUNCE_INTERVAL_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	default 1" >>$OUTPUT_FILE
	echo -e "	range -6 4 if (PORT${portStr}_INST${instStr}_PROFILE_KEEP_GLOBAL && GLOBAL_PROFILE_CUSTOM) \\" >>$OUTPUT_FILE
	echo -e "		      || PORT${portStr}_INST${instStr}_PROFILE_CUSTOM" >>$OUTPUT_FILE
	echo -e "	range 0 4" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  The mean time interval between transmissions of successive" >>$OUTPUT_FILE
	echo -e "	  Announce messages. The value is the logarithm to the base 2." >>$OUTPUT_FILE
	echo -e "	  The configurable range shall be 0 to 4." >>$OUTPUT_FILE


	echo -e "\nconfig PORT${portStr}_INST${instStr}_ANNOUNCE_RECEIPT_TIMEOUT_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	bool \"Overwrite default announceReceiptTimeout\"" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${portStr}_INST${instStr}_ANNOUNCE_RECEIPT_TIMEOUT" >>$OUTPUT_FILE
	echo -e "	int \"announceReceiptTimeout\" if PORT${portStr}_INST${instStr}_ANNOUNCE_RECEIPT_TIMEOUT_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	default 3" >>$OUTPUT_FILE
	echo -e "	range 2 255" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  The announceReceiptTimeout specifies the number of announceIntervals" >>$OUTPUT_FILE
	echo -e "	  that must pass without receipt of an Announce message before the" >>$OUTPUT_FILE
	echo -e "	  occurrence of the event ANNOUNCE_RECEIPT_TIMEOUT_EXPIRES." >>$OUTPUT_FILE
	echo -e "	  The value is the logarithm to the base 2." >>$OUTPUT_FILE
	echo -e "	  The configurable range shall be 2 to 255" >>$OUTPUT_FILE


	echo -e "\nconfig PORT${portStr}_INST${instStr}_SYNC_INTERVAL_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	bool \"Overwrite default logSyncInterval\"" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${portStr}_INST${instStr}_SYNC_INTERVAL" >>$OUTPUT_FILE
	echo -e "	int \"logSyncInterval\" if PORT${portStr}_INST${instStr}_SYNC_INTERVAL_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	default 0" >>$OUTPUT_FILE
	echo -e "	range -6 1 if (PORT${portStr}_INST${instStr}_PROFILE_KEEP_GLOBAL && GLOBAL_PROFILE_CUSTOM) \\" >>$OUTPUT_FILE
	echo -e "		      || PORT${portStr}_INST${instStr}_PROFILE_CUSTOM" >>$OUTPUT_FILE
	echo -e "	range -1 1" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  The mean time interval between transmission of successive" >>$OUTPUT_FILE
	echo -e "	  Sync messages, i.e., the sync-interval, when transmitted" >>$OUTPUT_FILE
	echo -e "	  as multicast messages. The value is the logarithm to the base 2." >>$OUTPUT_FILE
	echo -e "	  The configurable range shall be -1 to +1" >>$OUTPUT_FILE


	echo -e "\nconfig PORT${portStr}_INST${instStr}_MIN_DELAY_REQ_INTERVAL_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	bool \"Overwrite default minDelayRequestInterval\"" >>$OUTPUT_FILE
	echo -e "	depends on PORT${portStr}_INST${instStr}_MECHANISM_E2E" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${portStr}_INST${instStr}_MIN_DELAY_REQ_INTERVAL" >>$OUTPUT_FILE
	echo -e "	int \"minDelayRequestInterval\" if PORT${portStr}_INST${instStr}_MIN_DELAY_REQ_INTERVAL_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	default 0" >>$OUTPUT_FILE
	echo -e "	range -6 5 if (PORT${portStr}_INST${instStr}_PROFILE_KEEP_GLOBAL && GLOBAL_PROFILE_CUSTOM) \\" >>$OUTPUT_FILE
	echo -e "		      || PORT${portStr}_INST${instStr}_PROFILE_CUSTOM" >>$OUTPUT_FILE
	echo -e "	range 0 5" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  The minDelayRequestInterval specifies the minimum permitted" >>$OUTPUT_FILE
	echo -e "	  mean time interval between successive Delay_Req messages." >>$OUTPUT_FILE
	echo -e "	  The value is the logarithm to the base 2." >>$OUTPUT_FILE
	echo -e "	  The configurable range shall be 0 to 5" >>$OUTPUT_FILE


	echo -e "\nconfig PORT${portStr}_INST${instStr}_MIN_PDELAY_REQ_INTERVAL_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	bool \"Overwrite default minPDelayRequestInterval\"" >>$OUTPUT_FILE
	echo -e "	depends on PORT${portStr}_INST${instStr}_MECHANISM_P2P" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${portStr}_INST${instStr}_MIN_PDELAY_REQ_INTERVAL" >>$OUTPUT_FILE
	echo -e "	int \"minPDelayRequestInterval\" if PORT${portStr}_INST${instStr}_MIN_PDELAY_REQ_INTERVAL_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	default 0" >>$OUTPUT_FILE
	echo -e "	range -6 5 if (PORT${portStr}_INST${instStr}_PROFILE_KEEP_GLOBAL && GLOBAL_PROFILE_CUSTOM) \\" >>$OUTPUT_FILE
	echo -e "		      || PORT${portStr}_INST${instStr}_PROFILE_CUSTOM" >>$OUTPUT_FILE
	echo -e "	range 0 5" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  The minPDelayRequestInterval specifies the minimum permitted" >>$OUTPUT_FILE
	echo -e "	  mean time interval between successive Pdelay_Req messages." >>$OUTPUT_FILE
	echo -e "	  The value is the logarithm to the base 2." >>$OUTPUT_FILE
	echo -e "	  The configurable range shall be 0 to 5" >>$OUTPUT_FILE


# L1 sync
	echo -e "# Use following options if L1Sync or extension autonegotiation is enabled" >>$OUTPUT_FILE
	echo -e "\nif PORT${portStr}_INST${instStr}_EXTENSION_L1S || PORT${portStr}_INST${instStr}_EXTENSION_L1S_WR" >>$OUTPUT_FILE

	echo -e "\n# L1SYNC_ENABLED and its dependencies can be changed only for CUSTOM profile" >>$OUTPUT_FILE
	echo -e "comment \"Options specific to L1Sync\"" >>$OUTPUT_FILE

	echo -e "\n\nconfig PORT${portStr}_INST${instStr}_L1SYNC_ENABLED_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	bool \"Overwrite default value for enabling L1Sync (only for Custom profile)\"" >>$OUTPUT_FILE
	echo -e "	depends on (PORT${portStr}_INST${instStr}_PROFILE_CUSTOM || (PORT${portStr}_INST${instStr}_PROFILE_KEEP_GLOBAL && GLOBAL_PROFILE_CUSTOM))" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${portStr}_INST${instStr}_L1SYNC_ENABLED" >>$OUTPUT_FILE
	echo -e "	bool \"L1SyncBasicPortDS.L1SyncEnabled\" if PORT${portStr}_INST${instStr}_L1SYNC_ENABLED_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	depends on (PORT${portStr}_INST${instStr}_PROFILE_CUSTOM || (PORT${portStr}_INST${instStr}_PROFILE_KEEP_GLOBAL && GLOBAL_PROFILE_CUSTOM))" >>$OUTPUT_FILE
	echo -e "	default y" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  This parameter specifies whether the L1Sync option is enabled on the PTP Port. If" >>$OUTPUT_FILE
	echo -e "	  L1SyncEnabled is TRUE, then the L1Sync message exchange is supported and enabled." >>$OUTPUT_FILE
	echo -e "	  Used only for Custom profile." >>$OUTPUT_FILE


	echo -e "\n\nconfig PORT${portStr}_INST${instStr}_L1SYNC_INTERVAL_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	bool \"Overwrite default L1SyncBasicPortDS.logL1SyncInterval\"" >>$OUTPUT_FILE
	echo -e "	depends on !PORT${portStr}_INST${instStr}_L1SYNC_ENABLED_OVERWRITE || PORT${portStr}_INST${instStr}_L1SYNC_ENABLED" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${portStr}_INST${instStr}_L1SYNC_INTERVAL" >>$OUTPUT_FILE
	echo -e "	int \"L1SyncBasicPortDS.logL1SyncInterval\" if PORT${portStr}_INST${instStr}_L1SYNC_INTERVAL_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	depends on !PORT${portStr}_INST${instStr}_L1SYNC_ENABLED_OVERWRITE || PORT${portStr}_INST${instStr}_L1SYNC_ENABLED" >>$OUTPUT_FILE
	echo -e "	default 0" >>$OUTPUT_FILE
	echo -e "	range -4 4" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  The L1Sync interval specifies the time interval" >>$OUTPUT_FILE
	echo -e "	  between successive periodic L1_SYNC TLV." >>$OUTPUT_FILE
	echo -e "	  The value is the logarithm to the base 2." >>$OUTPUT_FILE
	echo -e "	  The configurable range shall be -4 to 4." >>$OUTPUT_FILE


	echo -e "\n\nconfig PORT${portStr}_INST${instStr}_L1SYNC_RECEIPT_TIMEOUT_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	bool \"Overwrite default L1SyncBasicPortDS.L1SyncReceiptTimeout\"" >>$OUTPUT_FILE
	echo -e "	depends on !PORT${portStr}_INST${instStr}_L1SYNC_ENABLED_OVERWRITE || PORT${portStr}_INST${instStr}_L1SYNC_ENABLED" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${portStr}_INST${instStr}_L1SYNC_RECEIPT_TIMEOUT" >>$OUTPUT_FILE
	echo -e "	int \"L1SyncBasicPortDS.L1SyncReceiptTimeout\" if PORT${portStr}_INST${instStr}_L1SYNC_RECEIPT_TIMEOUT_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	depends on !PORT${portStr}_INST${instStr}_L1SYNC_ENABLED_OVERWRITE || PORT${portStr}_INST${instStr}_L1SYNC_ENABLED" >>$OUTPUT_FILE
	echo -e "	default 3" >>$OUTPUT_FILE
	echo -e "	range 2 10" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  The value of L1SyncReceiptTimeout specifies the number of elapsed" >>$OUTPUT_FILE
	echo -e "	  L1SyncIntervals that must pass without reception of the L1_SYNC TLV" >>$OUTPUT_FILE
	echo -e "	  before the L1_SYNC TLV reception timeout occurs." >>$OUTPUT_FILE
	echo -e "	  The value is the logarithm to the base 2." >>$OUTPUT_FILE
	echo -e "	  The configurable range shall be 2 to 10" >>$OUTPUT_FILE


	echo -e "\n\nconfig PORT${portStr}_INST${instStr}_L1SYNC_TX_COHERENT_IS_REQUIRED_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	bool \"Overwrite default L1SyncBasicPortDS.txCoherentIsRequired\"" >>$OUTPUT_FILE
	echo -e "	depends on PORT${portStr}_INST${instStr}_L1SYNC_ENABLED" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${portStr}_INST${instStr}_L1SYNC_TX_COHERENT_IS_REQUIRED" >>$OUTPUT_FILE
	echo -e "	bool \"L1SyncBasicPortDS.txCoherentIsRequired\" if PORT${portStr}_INST${instStr}_L1SYNC_TX_COHERENT_IS_REQUIRED_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	depends on PORT${portStr}_INST${instStr}_L1SYNC_ENABLED" >>$OUTPUT_FILE
	echo -e "	default y" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	   The Boolean attribute txCoherentIsRequired specifies the configuration of the L1Sync port and the" >>$OUTPUT_FILE
	echo -e "	   expected configuration of its peer L1Sync port. This configuration indicates whether the L1Sync port is" >>$OUTPUT_FILE
	echo -e "	   required to be a transmit coherent port." >>$OUTPUT_FILE


	echo -e "\n\nconfig PORT${portStr}_INST${instStr}_L1SYNC_RX_COHERENT_IS_REQUIRED_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	bool \"Overwrite default L1SyncBasicPortDS.rxCoherentIsRequired\"" >>$OUTPUT_FILE
	echo -e "	depends on PORT${portStr}_INST${instStr}_L1SYNC_ENABLED" >>$OUTPUT_FILE
 
	echo -e "\nconfig PORT${portStr}_INST${instStr}_L1SYNC_RX_COHERENT_IS_REQUIRED" >>$OUTPUT_FILE
	echo -e "	bool \"L1SyncBasicPortDS.rxCoherentIsRequired\" if PORT${portStr}_INST${instStr}_L1SYNC_RX_COHERENT_IS_REQUIRED_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	depends on PORT${portStr}_INST${instStr}_L1SYNC_ENABLED" >>$OUTPUT_FILE
	echo -e "	default y" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  The Boolean attribute rxCoherentIsRequired specifies the configuration of the L1Sync port and the" >>$OUTPUT_FILE
	echo -e "	  expected configuration of its peer L1Sync port. This configuration indicates whether the L1Sync port is" >>$OUTPUT_FILE
	echo -e "	  required to be a receive coherent port." >>$OUTPUT_FILE


	echo -e "\n\nconfig PORT${portStr}_INST${instStr}_L1SYNC_CONGRUENT_IS_REQUIRED_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	bool \"Overwrite default L1SyncBasicPortDS.congruentIsRequired\"" >>$OUTPUT_FILE
	echo -e "	depends on PORT${portStr}_INST${instStr}_L1SYNC_ENABLED" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${portStr}_INST${instStr}_L1SYNC_CONGRUENT_IS_REQUIRED" >>$OUTPUT_FILE
	echo -e "	bool \"L1SyncBasicPortDS.congruentIsRequired\" if PORT${portStr}_INST${instStr}_L1SYNC_CONGRUENT_IS_REQUIRED_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	depends on PORT${portStr}_INST${instStr}_L1SYNC_ENABLED" >>$OUTPUT_FILE
	echo -e "	default y" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  The Boolean attribute congruentIsRequired specifies configuration of the L1Sync port and the expected" >>$OUTPUT_FILE
	echo -e "	  configuration of its peer L1Sync port. This configuration indicates whether the L1Sync port is required to" >>$OUTPUT_FILE
	echo -e "	  be a congruent port" >>$OUTPUT_FILE


	echo -e "\n\nconfig PORT${portStr}_INST${instStr}_L1SYNC_OPT_PARAMS_ENABLED_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	bool \"Overwrite default L1SyncBasicPortDS.optParamsEnabled\"" >>$OUTPUT_FILE
	echo -e "	depends on PORT${portStr}_INST${instStr}_L1SYNC_ENABLED" >>$OUTPUT_FILE
 
	echo -e "\nconfig PORT${portStr}_INST${instStr}_L1SYNC_OPT_PARAMS_ENABLED" >>$OUTPUT_FILE
	echo -e "	bool \"L1SyncBasicPortDS.optParamsEnabled\" if PORT${portStr}_INST${instStr}_L1SYNC_OPT_PARAMS_ENABLED_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	depends on PORT${portStr}_INST${instStr}_L1SYNC_ENABLED" >>$OUTPUT_FILE
	echo -e "	default n" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  The Boolean attribute optParamsEnabled specifies whether the L1Sync port transmitting the L1_SYNC" >>$OUTPUT_FILE
	echo -e "	  TLV extends this TLV with the information about the optional parameters." >>$OUTPUT_FILE


	echo -e "\n\nconfig PORT${portStr}_INST${instStr}_L1SYNC_OPT_PARAMS_TS_CORRECTED_TX_ENABLED_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	bool \"Overwrite default L1SyncBasicPortDS.txCoherentIsRequired\"" >>$OUTPUT_FILE
	echo -e "	depends on PORT${portStr}_INST${instStr}_L1SYNC_OPT_PARAMS_ENABLED" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${portStr}_INST${instStr}_L1SYNC_OPT_PARAMS_TS_CORRECTED_TX_ENABLED" >>$OUTPUT_FILE
	echo -e "	bool \"L1SyncBasicPortDS.timestampsCorrectedTx\" if PORT${portStr}_INST${instStr}_L1SYNC_OPT_PARAMS_TS_CORRECTED_TX_ENABLED_OVERWRITE" >>$OUTPUT_FILE
	echo -e "	depends on PORT${portStr}_INST${instStr}_L1SYNC_OPT_PARAMS_ENABLED" >>$OUTPUT_FILE
	echo -e "	default n" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  When L1SyncOptParamsPortDS.timestampsCorrectedTx is TRUE," >>$OUTPUT_FILE
	echo -e "	  the L1Sync port shall correct the transmitted egress timestamps with" >>$OUTPUT_FILE
	echo -e "	  the known value of the phase offset." >>$OUTPUT_FILE
	echo -e "\nendif # PORT${portStr}_INST${instStr}_EXTENSION_L1S || PORT${portStr}_INST${instStr}_EXTENSION_L1S_WR" >>$OUTPUT_FILE
	echo -e "" >>$OUTPUT_FILE
}

function print_instance_footer() { 
	local portIdx=$1
	local portStr=`printf "%02d" $portIdx`

	local instIdx=$2
	local instStr=`printf "%02d" $instIdx`

	local t24p=${port_t24p[$1]}

	echo -e "\nendmenu # Port ${portIdx} Instance ${instIdx}" >>$OUTPUT_FILE

	echo -e "\n# Keep T24P_TRANS_POINT also for ports without instances" >>$OUTPUT_FILE
	echo -e "config PORT${portStr}_INST${instStr}_T24P_TRANS_POINT" >>$OUTPUT_FILE
	echo -e "	int" >>$OUTPUT_FILE
	echo -e "	default ${t24p}" >>$OUTPUT_FILE
}

declare -A port_tx=(
	[1]=240859 [2]=240246 [3]=240468 [4]=241377 [5]=239190 [6]=238521
	[7]=239460 [8]=238804 [9]=240569 [10]=240768 [11]=240986 [12]=241876
	[13]=227793 [14]=228023 [15]=228208 [16]=228362 [17]=228608 [18]=228740
)

declare -A port_rx=(
	[1]=280509 [2]=279646 [3]=278778 [4]=277767 [5]=278370 [6]=278635
	[7]=279092 [8]=277660 [9]=278977 [10]=278356 [11]=280456 [12]=280824
	[13]=231537 [14]=231569 [15]=235674 [16]=235918 [17]=231252 [18]=230680
)

declare -A port_t24p=(
	[1]=10800 [2]=12650 [3]=13800 [4]=14650 [5]=13300 [6]=13300
	[7]=13000 [8]=14950 [9]=14500 [10]=13350 [11]=11150 [12]=8500
	[13]=14650 [14]=14850 [15]=11200 [16]=11000 [17]=14150 [18]=14900
)

# Generation parameters
portCount=18
instCount=1
rm $OUTPUT_FILE
print_header

for p in `seq 1 $portCount`; do

	print_port_header ${p}

	for i in `seq 1 $instCount`; do
		print_instance_header $p $i
		print_instance_footer $p $i
	done
	
	print_port_footer $i
	
done

print_footer


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

	echo -e "menu \"Port Timing Configuration\"" >$OUTPUT_FILE
	echo -e "config PTP_OPT_EXT_PORT_CONFIG_ENABLED" >>$OUTPUT_FILE
	echo -e "\tbool \"externalPortConfigurationEnabled\" " >>$OUTPUT_FILE
	echo -e "\tdefault true" >>$OUTPUT_FILE
	echo -e "\thelp" >>$OUTPUT_FILE
	echo -e "\t  This option is used by the high accuracy profile to force the port state." >>$OUTPUT_FILE  
	echo -e "\t  When set, BMCA is disabled." >>$OUTPUT_FILE  
	echo -e "\t  For more details please refer to the IEEE 1588-20019 (clause 17.6.2)" >>$OUTPUT_FILE

	echo -e "\nconfig PTP_SLAVE_ONLY" >>$OUTPUT_FILE
	echo -e "\tdepends on PTP_OPT_EXT_PORT_CONFIG_ENABLED=\"n\" " >>$OUTPUT_FILE
	echo -e "\tbool \"slaveOnly\" " >>$OUTPUT_FILE
	echo -e "\tdefault n" >>$OUTPUT_FILE
	echo -e "\thelp" >>$OUTPUT_FILE
	echo -e "\t  A slaveOnly Ordinary Clock utilizes the slaveOnly state machine" >>$OUTPUT_FILE
	echo -e "\t  which does not enable transition to MASTER state." >>$OUTPUT_FILE
	echo -e "\t  For more details please refer to the IEEE 1588-20019 (clause 9.2.2.1)" >>$OUTPUT_FILE
}

function print_footer() {
 
	echo -e "\nendmenu" >>$OUTPUT_FILE
 
}

function print_port_header() { 
	#remove leading zero from i_port (params has numbers with leading zero,
	#interface names are without leading zero)
	local portIdx=$(expr $1 + 0)
	
	echo -e "\nmenu \"PORT ${portIdx}\"" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${1}_IFACE" >>$OUTPUT_FILE
	echo -e "\tstring \"Network interface\"" >>$OUTPUT_FILE
	echo -e "\tdefault \"wri${portIdx}\"" >>$OUTPUT_FILE
	echo -e "\thelp" >>$OUTPUT_FILE
	echo -e "\t  Used to set the physical port interface name: \"wri[1-18]\"" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${1}_FIBER" >>$OUTPUT_FILE
	echo -e "\tint  \"Fiber type\"" >>$OUTPUT_FILE
	echo -e "\tdefault 0" >>$OUTPUT_FILE
	echo -e "\thelp" >>$OUTPUT_FILE
	echo -e "\t  Used to set the type of fiber (number referring to the corresponding " >>$OUTPUT_FILE
	echo -e "\t  FIBERXX_PARAMS)" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${1}_CONSTANT_ASYMMETRY" >>$OUTPUT_FILE
	echo -e "\tint \"asymmetryCorrectionPortDS.constantAsymmetry\"" >>$OUTPUT_FILE
	echo -e "\tdefault 0" >>$OUTPUT_FILE
	echo -e "\thelp" >>$OUTPUT_FILE
	echo -e "\t   Used to set the constant delay asymmetry." >>$OUTPUT_FILE

	echo -e "\nchoice" >>$OUTPUT_FILE
	echo -e "\tprompt \"Number of port instances\"" >>$OUTPUT_FILE
	echo -e "\tdefault PORT${1}_INSTANCE_COUNT_1" >>$OUTPUT_FILE
	echo -e "\tconfig PORT${1}_INSTANCE_COUNT_0" >>$OUTPUT_FILE
	echo -e "\t  bool \"0\"" >>$OUTPUT_FILE
	echo -e "\tconfig PORT${1}_INSTANCE_COUNT_1" >>$OUTPUT_FILE
	echo -e "\t  bool \"1\"" >>$OUTPUT_FILE
	echo -e "\tconfig PORT${1}_INSTANCE_COUNT_2" >>$OUTPUT_FILE
	echo -e "\t  bool \"2\"" >>$OUTPUT_FILE
	echo -e "endchoice" >>$OUTPUT_FILE

} 

function print_port_footer() { 
 
	echo -e "\nendmenu" >>$OUTPUT_FILE
 
} 

function print_instance_header() { 
	#remove leading zero from i_port (params has numbers with leading zero,
	#interface names are without leading zero)
	local portIdx=$(expr $1 + 0)
	local instIdx=$(expr $2 + 0)
	
	if [ $instIdx -eq 1 ] ; then prof=WR ; else prof=HA ; fi
	local tx=${port_tx[$1]}
	local rx=${port_rx[$1]}
	echo -e "\nmenu \"Instance ${instIdx}\"" >>$OUTPUT_FILE
	if [ $instIdx -eq 1 ] ; then 
		echo -e "	depends on PORT${1}_INSTANCE_COUNT_1 ||  PORT${1}_INSTANCE_COUNT_2  " >>$OUTPUT_FILE
	else 
		echo -e "	depends on PORT${1}_INSTANCE_COUNT_2  " >>$OUTPUT_FILE
	fi

	echo -e "\nchoice" >>$OUTPUT_FILE
	echo -e "    prompt \"Network protocol\"" >>$OUTPUT_FILE
	echo -e "    default PORT${1}_INST${2}_PROTOCOL_RAW" >>$OUTPUT_FILE
	echo -e "    config PORT${1}_INST${2}_PROTOCOL_RAW" >>$OUTPUT_FILE
	echo -e "        bool \"IEEE 802.3\"" >>$OUTPUT_FILE
	echo -e "    config PORT${1}_INST${2}_PROTOCOL_UDP_IPV4" >>$OUTPUT_FILE
	echo -e "        bool \"UDP/Ipv4\"" >>$OUTPUT_FILE
	echo -e "endchoice" >>$OUTPUT_FILE

	echo -e "\nchoice" >>$OUTPUT_FILE
	echo -e "    prompt \"Delay mechanism\"" >>$OUTPUT_FILE
	echo -e "    default PORT${1}_INST${2}_MECHANISM_E2E" >>$OUTPUT_FILE
	echo -e "    config PORT${1}_INST${2}_MECHANISM_E2E" >>$OUTPUT_FILE
	echo -e "        bool \"End-to-end\"" >>$OUTPUT_FILE
	echo -e "    config PORT${1}_INST${2}_MECHANISM_P2P" >>$OUTPUT_FILE
	echo -e "        bool \"Peer-to-peer\"" >>$OUTPUT_FILE
	echo -e "endchoice" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${1}_INST${2}_MONITOR" >>$OUTPUT_FILE
	echo -e "	bool \"SNMP monitoring\"" >>$OUTPUT_FILE
	echo -e "	default y" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  Option to disable or enable triggering errors in SNMP on a port" >>$OUTPUT_FILE
	
	echo -e "\nchoice" >>$OUTPUT_FILE
	echo -e "    prompt \"Profile\"" >>$OUTPUT_FILE
	echo -e "    default PORT${1}_INST${2}_PROFILE_$defaultProfile" >>$OUTPUT_FILE
	for profile in $profileList; do
		echo -e "    config PORT${1}_INST${2}_PROFILE_$profile" >>$OUTPUT_FILE
		echo -e "        bool \"${profileNames[$profile]}\"" >>$OUTPUT_FILE	
	done
	echo -e "endchoice" >>$OUTPUT_FILE
	
	echo -e "\nchoice" >>$OUTPUT_FILE
	echo -e "    prompt \"Desired state\"" >>$OUTPUT_FILE
	echo -e "    depends on PTP_OPT_EXT_PORT_CONFIG_ENABLED" >>$OUTPUT_FILE
	[[ $portIdx -eq 1 ]] && echo -e "    default PORT${1}_INST${2}_DESIRADE_STATE_SLAVE if TIME_BC" >>$OUTPUT_FILE
	echo -e "    default PORT${1}_INST${2}_DESIRADE_STATE_MASTER" >>$OUTPUT_FILE
	echo -e "    config PORT${1}_INST${2}_DESIRADE_STATE_MASTER" >>$OUTPUT_FILE
	echo -e "        bool \"Master\"" >>$OUTPUT_FILE
	echo -e "    config PORT${1}_INST${2}_DESIRADE_STATE_SLAVE" >>$OUTPUT_FILE
	echo -e "        bool \"Slave\"" >>$OUTPUT_FILE
	echo -e "    config PORT${1}_INST${2}_DESIRADE_STATE_PASSIVE" >>$OUTPUT_FILE
	echo -e "        bool \"Passive\"" >>$OUTPUT_FILE
	echo -e "endchoice" >>$OUTPUT_FILE
	
	echo -e "\nconfig PORT${1}_INST${2}_ASYMMETRY_CORRECTION_ENABLE" >>$OUTPUT_FILE
	echo -e "	depends on !PORT${1}_INST${2}_PROFILE_HA && !PORT${1}_INST${2}_PROFILE_WR" >>$OUTPUT_FILE
	echo -e "    bool \"asymmetryCorrectionPortDS.enable\"" >>$OUTPUT_FILE
	echo -e "    default y" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  When supported, the value TRUE shall indicate that the mechanism of for the calculation" >>$OUTPUT_FILE
	echo -e "	  of the <delayAsymmetry> for certain media is enabled on the PTP port." >>$OUTPUT_FILE
	
	echo -e "\nchoice" >>$OUTPUT_FILE
	echo -e "    prompt \"BMCA mode\"" >>$OUTPUT_FILE
	echo -e "    depends on PTP_OPT_EXT_PORT_CONFIG_ENABLED!=y" >>$OUTPUT_FILE
	echo -e "    default PORT${1}_INST${2}_BMODE_MASTER_ONLY if TIME_BC" >>$OUTPUT_FILE
	echo -e "    default PORT${1}_INST${2}_BMODE_AUTO" >>$OUTPUT_FILE
	echo -e "    config PORT${1}_INST${2}_BMODE_MASTER_ONLY" >>$OUTPUT_FILE
	echo -e "        bool \"MasterOnly\"" >>$OUTPUT_FILE
	echo -e "    config PORT${1}_INST${2}_BMODE_AUTO" >>$OUTPUT_FILE
	echo -e "        bool \"Auto\"" >>$OUTPUT_FILE
	echo -e "endchoice" >>$OUTPUT_FILE
	
	echo -e "\nconfig PORT${1}_INST${2}_EGRESS_LATENCY" >>$OUTPUT_FILE
	echo -e "    int \"timestampCorrectionPortDS.egressLatency (ps)\"" >>$OUTPUT_FILE
	echo -e "    default ${tx}" >>$OUTPUT_FILE
	echo -e " help" >>$OUTPUT_FILE
	echo -e "	 Defines the transmission constant delay (ps)" >>$OUTPUT_FILE
		
	echo -e "\nconfig PORT${1}_INST${2}_INGRESS_LATENCY" >>$OUTPUT_FILE
	echo -e "    int \"timestampCorrectionPortDS.ingressLatency (ps)\"" >>$OUTPUT_FILE
	echo -e "    default ${rx}" >>$OUTPUT_FILE
	echo -e " help" >>$OUTPUT_FILE
	echo -e "	 Defines the reception constant delay (ps)" >>$OUTPUT_FILE
		
	echo -e "\nconfig PORT${1}_INST${2}_ANNOUNCE_INTERVAL" >>$OUTPUT_FILE
	echo -e "	int \"logAnnounceInterval\" " >>$OUTPUT_FILE
	echo -e "	default 1" >>$OUTPUT_FILE
	echo -e "	range 0 4" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  The mean time interval between transmissions of successive" >>$OUTPUT_FILE
	echo -e "	  Announce messages. The value is the logarithm to the base 2." >>$OUTPUT_FILE
	echo -e "	  The configurable range shall be 0 to 4." >>$OUTPUT_FILE

	echo -e "\nconfig PORT${1}_INST${2}_ANNOUNCE_RECEIPT_TIMEOUT" >>$OUTPUT_FILE
	echo -e "	int \"announceReceiptTimeout\"" >>$OUTPUT_FILE
	echo -e "	default 3" >>$OUTPUT_FILE
	echo -e "	range 2 255" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  The announceReceiptTimeout specifies the number of announceIntervals " >>$OUTPUT_FILE
	echo -e "	  that must pass without receipt of an Announce message before the " >>$OUTPUT_FILE
	echo -e "	  occurrence of the event ANNOUNCE_RECEIPT_TIMEOUT_EXPIRES." >>$OUTPUT_FILE
	echo -e "	  The value is the logarithm to the base 2." >>$OUTPUT_FILE
	echo -e "	  The configurable range shall be 2 to 255" >>$OUTPUT_FILE
		
	echo -e "\nconfig PORT${1}_INST${2}_SYNC_INTERVAL" >>$OUTPUT_FILE
	echo -e "	int \"logSyncInterval\"" >>$OUTPUT_FILE
	echo -e "	default 0" >>$OUTPUT_FILE
	echo -e "	range -1 1" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  The mean time interval between transmission of successive" >>$OUTPUT_FILE
	echo -e "	  Sync messages, i.e., the sync-interval, when transmitted" >>$OUTPUT_FILE
	echo -e "	  as multicast messages. The value is the logarithm to the base 2." >>$OUTPUT_FILE
	echo -e "	  The configurable range shall be -1 to +1" >>$OUTPUT_FILE
		
	echo -e "\nconfig PORT${1}_INST${2}_MIN_DELAY_REQ_INTERVAL" >>$OUTPUT_FILE
	echo -e "	depends on PORT${1}_INST${2}_MECHANISM_E2E  " >>$OUTPUT_FILE
	echo -e "	int \"minDelayRequestInterval\"" >>$OUTPUT_FILE
	echo -e "	default 0" >>$OUTPUT_FILE
	echo -e "	range 0 5" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  The minDelayRequestInterval specifies the minimum permitted" >>$OUTPUT_FILE
	echo -e "	  mean time interval between successive Delay_Req messages." >>$OUTPUT_FILE
	echo -e "	  The value is the logarithm to the base 2." >>$OUTPUT_FILE
	echo -e "	  The configurable range shall be 0 to 5" >>$OUTPUT_FILE
		
	echo -e "\nconfig PORT${1}_INST${2}_MIN_PDELAY_REQ_INTERVAL" >>$OUTPUT_FILE
	echo -e "	depends on PORT${1}_INST${2}_MECHANISM_P2P  " >>$OUTPUT_FILE
	echo -e "	int \"minPDelayRequestInterval\"" >>$OUTPUT_FILE
	echo -e "	default 0" >>$OUTPUT_FILE
	echo -e "	range 0 5" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  The minPDelayRequestInterval specifies the minimum permitted" >>$OUTPUT_FILE
	echo -e "	  mean time interval between successive Pdelay_Req messages." >>$OUTPUT_FILE
	echo -e "	  The value is the logarithm to the base 2." >>$OUTPUT_FILE
	echo -e "	  The configurable range shall be 0 to 5" >>$OUTPUT_FILE
		
	echo -e "\nconfig PORT${1}_INST${2}_L1SYNC_ENABLED" >>$OUTPUT_FILE
	echo -e "	depends on PORT${1}_INST${2}_PROFILE_CUSTOM  " >>$OUTPUT_FILE
	echo -e "	bool \"L1SyncBasicPortDS.L1SyncEnabled\"" >>$OUTPUT_FILE
	echo -e "	default y" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  This parameter specifies whether the L1Sync option is enabled on the PTP Port. If" >>$OUTPUT_FILE
	echo -e "	  L1SyncEnabled is TRUE, then the L1Sync message exchange is supported and enabled" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${1}_INST${2}_L1SYNC_INTERVAL" >>$OUTPUT_FILE
	echo -e "	depends on PORT${1}_INST${2}_PROFILE_HA || (PORT${1}_INST${2}_PROFILE_CUSTOM &&  PORT${1}_INST${2}_L1SYNC_ENABLED=\"y\") " >>$OUTPUT_FILE
	echo -e "	int \"L1SyncBasicPortDS.logL1SyncInterval\"" >>$OUTPUT_FILE
	echo -e "	default 0" >>$OUTPUT_FILE
	echo -e "	range -4 4" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  The L1Sync interval specifies the time interval" >>$OUTPUT_FILE
	echo -e "	  between successive periodic L1_SYNC TLV." >>$OUTPUT_FILE
	echo -e "	  The value is the logarithm to the base 2." >>$OUTPUT_FILE
	echo -e "	  The configurable range shall be -4 to 4" >>$OUTPUT_FILE
		
	echo -e "\nconfig PORT${1}_INST${2}_L1SYNC_RECEIPT_TIMEOUT" >>$OUTPUT_FILE
	echo -e "	depends on PORT${1}_INST${2}_PROFILE_HA || (PORT${1}_INST${2}_PROFILE_CUSTOM &&  PORT${1}_INST${2}_L1SYNC_ENABLED=\"y\") " >>$OUTPUT_FILE
	echo -e "	int \"L1SyncBasicPortDS.L1SyncReceiptTimeout\"" >>$OUTPUT_FILE
	echo -e "	default 3" >>$OUTPUT_FILE
	echo -e "	range 2 10" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  The value of L1SyncReceiptTimeout specifies the number of elapsed " >>$OUTPUT_FILE
	echo -e "	  L1SyncIntervals that must pass without reception of the L1_SYNC TLV " >>$OUTPUT_FILE
	echo -e "	  before the L1_SYNC TLV reception timeout occurs." >>$OUTPUT_FILE
	echo -e "	  The value is the logarithm to the base 2." >>$OUTPUT_FILE
	echo -e "	  The configurable range shall be 2 to 10" >>$OUTPUT_FILE
	
	echo -e "\nconfig PORT${1}_INST${2}_L1SYNC_TX_COHERENCY_IS_REQUIRED" >>$OUTPUT_FILE
	echo -e "	depends on PORT${1}_INST${2}_PROFILE_CUSTOM &&  PORT${1}_INST${2}_L1SYNC_ENABLED=\"y\" " >>$OUTPUT_FILE
	echo -e "	bool \"L1SyncBasicPortDS.txCoherencyIsRequired\"" >>$OUTPUT_FILE
	echo -e "	default y" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	   The Boolean attribute txCoherentIsRequired specifies the configuration of the L1Sync port and the" >>$OUTPUT_FILE
	echo -e "	   expected configuration of its peer L1Sync port. This configuration indicates whether the L1Sync port is" >>$OUTPUT_FILE
	echo -e "	   required to be a transmit coherent port." >>$OUTPUT_FILE

	echo -e "\nconfig PORT${1}_INST${2}_L1SYNC_RX_COHERENCY_IS_REQUIRED" >>$OUTPUT_FILE
	echo -e "	depends on PORT${1}_INST${2}_PROFILE_CUSTOM &&  PORT${1}_INST${2}_L1SYNC_ENABLED=\"y\" " >>$OUTPUT_FILE
	echo -e "	bool \"L1SyncBasicPortDS.rxCoherencyIsRequired\"" >>$OUTPUT_FILE
	echo -e "	default y" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  The Boolean attribute rxCoherentIsRequired specifies the configuration of the L1Sync port and the" >>$OUTPUT_FILE
	echo -e "	  expected configuration of its peer L1Sync port. This configuration indicates whether the L1Sync port is" >>$OUTPUT_FILE
	echo -e "	  required to be a receive coherent port." >>$OUTPUT_FILE

	echo -e "\nconfig PORT${1}_INST${2}_L1SYNC_CONGRUENCY_IS_REQUIRED" >>$OUTPUT_FILE
	echo -e "	depends on PORT${1}_INST${2}_PROFILE_CUSTOM &&  PORT${1}_INST${2}_L1SYNC_ENABLED=\"y\" " >>$OUTPUT_FILE
	echo -e "	bool \"L1SyncBasicPortDS.congruencyIsRequired\"" >>$OUTPUT_FILE
	echo -e "	default y" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  The Boolean attribute congruentIsRequired specifies configuration of the L1Sync port and the expected" >>$OUTPUT_FILE
	echo -e "	  configuration of its peer L1Sync port. This configuration indicates whether the L1Sync port is required to" >>$OUTPUT_FILE
	echo -e "	  be a congruent port" >>$OUTPUT_FILE

	echo -e "\nconfig PORT${1}_INST${2}_L1SYNC_OPT_PARAMS_ENABLED" >>$OUTPUT_FILE
	echo -e "	depends on PORT${1}_INST${2}_PROFILE_CUSTOM &&  PORT${1}_INST${2}_L1SYNC_ENABLED=\"y\" " >>$OUTPUT_FILE
	echo -e "	bool \"L1SyncBasicPortDS.optParamsEnabled\"" >>$OUTPUT_FILE
	echo -e "	default n" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  The Boolean attribute optParamsEnabled specifies whether the L1Sync port transmitting the L1_SYNC" >>$OUTPUT_FILE
	echo -e "	  TLV extends this TLV with the information about the optional parameters." >>$OUTPUT_FILE

	echo -e "\nconfig PORT${1}_INST${2}_L1SYNC_OPT_PARAMS_TS_CORRECTED_TX_ENABLED" >>$OUTPUT_FILE
	echo -e "	depends on PORT${1}_INST${2}_L1SYNC_OPT_PARAMS_ENABLED=\"y\" " >>$OUTPUT_FILE
	echo -e "	bool \"L1SyncBasicPortDS.timestampsCorrectedTx\"" >>$OUTPUT_FILE
	echo -e "	default n" >>$OUTPUT_FILE
	echo -e "	help" >>$OUTPUT_FILE
	echo -e "	  When L1SyncOptParamsPortDS.timestampsCorrectedTx is TRUE, " >>$OUTPUT_FILE
	echo -e "	  the L1Sync port shall correct the transmitted egress timestamps with " >>$OUTPUT_FILE
	echo -e "	  the known value of the phase offset." >>$OUTPUT_FILE
}

function print_instance_footer() { 
 
	echo -e "\nendmenu" >>$OUTPUT_FILE
}

declare -A port_tx=(
	[01]=223897 [02]=224037 [03]=224142 [04]=224313 [05]=224455 [06]=224603
	[07]=224761 [08]=224898 [09]=225069 [10]=225245 [11]=225463 [12]=225645
	[13]=225801 [14]=225983 [15]=226208 [16]=226393 [17]=226594 [18]=226737
)

declare -A port_rx=(
	[01]=226273 [02]=226377 [03]=226638 [04]=226471 [05]=227679 [06]=227891
	[07]=228055 [08]=228178 [09]=228277 [10]=228435 [11]=228963 [12]=229107
	[13]=229225 [14]=229463 [15]=229850 [16]=229907 [17]=230106 [18]=230273
)

# Profile configuration
defaultProfile="WR"
profileList="PTP WR CUSTOM"
declare -A profileNames=(
	[PTP]="PTP" [HA]="High Accuracy" [WR]="White Rabbit" [CUSTOM]="Custom"
)

print_header

for i_port in {01..18}; do
	print_port_header ${i_port}

	for i_inst in {01..02}; do
		print_instance_header ${i_port} ${i_inst}
		print_instance_footer ${i_port} ${i_inst}
	done
	
	print_port_footer ${i_port}
	
done

print_footer


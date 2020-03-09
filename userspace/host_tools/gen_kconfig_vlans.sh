#!/bin/bash

# Jean-Claude BAU @CERN
# script to generate Kconfig timing port configuration.
#
# Parameters:
#     -o file Overwrite the default output file name
#

OUTPUT_FILE="Kconfig_vlans.in"

script_name="$0"

#decode script parameters
while getopts o: option
do
	case "${option}" in
		"o") OUTPUT_FILE=${OPTARG};;
	esac
done

function is_profile_enabled() {
	for profile in $profileList; do
		if [ $profile == "$1" ]; then return 1 ; fi
	done
	return 0;
}

function print_header() { 
	echo -e "\nmenu \"VLANs\""
	echo -e "\nconfig VLANS_ENABLE"
	echo -e "\tbool \"Enable VLANs\""
	echo -e "\tdefault n"
	echo -e "\thelp"
	echo -e "\t  Enable VLAN configuration via dot-config"
	echo -e "\nconfig VLANS_RAW_PORT_CONFIG"
	echo -e "\tdepends on VLANS_ENABLE"
	echo -e "\tbool \"Enable raw configuration for VLANS\""
	echo -e "\tdefault n"
	echo -e "\thelp"
	echo -e "\t  Enable raw configuration for VLANS\n"
}

function print_footer() {
	echo -e "\nendmenu # Menu VLANS" 
}

function print_ports_header() {
	echo -e "\nmenu \"Ports configuration\""
	echo -e "\tdepends on VLANS_ENABLE"
}

function print_ports_footer() {
	echo -e "\nendmenu  # Menu Ports configuration" 
}

function print_port_header() { 
	local portIdx=$1
	echo -e "\ncomment \"========= P O R T  ${portIdx} ============\"" 
} 

function print_port_footer() { 
	local portIdx=$1
} 

function print_port_config() {
	local portIdx=$1
	local portStr=`printf "%02d" $portIdx`
	
	echo -e "\nchoice VLANS_PORT${portStr}_MODE\n"
	echo -e "\tprompt \"VLAN mode\""
	echo -e "\tdefault VLANS_PORT${portStr}_MODE_UNQUALIFIED"
	help_vlan_port_mode

	echo -e "\nconfig VLANS_PORT${portStr}_MODE_ACCESS"
	echo -e "\tbool \"Access mode\""
	echo -e "\thelp"
	echo -e "\t  Please check the help of VLANS_PORT${portStr}_MODE"

	echo -e "\nconfig VLANS_PORT${portStr}_MODE_TRUNK"
	echo -e "\tbool \"Trunk mode\""
	echo -e "\thelp"
	echo -e "\t Please check the help of VLANS_PORT${portStr}_MODE"

	echo -e "\nconfig VLANS_PORT${portStr}_MODE_DISABLED"
	echo -e "\tbool \"VLAN-disabled mode\""
	echo -e "\thelp"
	echo -e "\t  Please check the help of VLANS_PORT${portStr}_MODE"

	echo -e "\nconfig VLANS_PORT${portStr}_MODE_UNQUALIFIED"
	echo -e "\tbool \"Unqualified mode\""
	echo -e "\thelp"
	echo -e "\t Please check the help of VLANS_PORT${portStr}_MODE"

	echo -e "endchoice # choice VLANS_PORT${portStr}_MODE"

	echo -e "\nchoice VLANS_PORT${portStr}_UNTAG"
	echo -e "\tprompt \"Untag frames\""
	echo -e "\tdefault VLANS_PORT${portStr}_UNTAG_ALL"
	echo -e "\tdepends on VLANS_PORT${portStr}_MODE_ACCESS || VLANS_RAW_PORT_CONFIG"
	echo -e "\thelp"
	echo -e "\t Decide whether VLAN-tags should be removed"

	echo -e "\nconfig VLANS_PORT${portStr}_UNTAG_ALL"
	echo -e "\tbool \"untag all\""
	echo -e "\thelp"
	echo -e "\t Untag all tagged frames."

	echo -e "\nconfig VLANS_PORT${portStr}_UNTAG_NONE"
	echo -e "\tbool \"untag none\""
	echo -e "\thelp"
	echo -e "\t Keep VLAN tags for all tagged frames."

	echo -e "endchoice #choice VLANS_PORT${portStr}_UNTAG"

	echo -e "\nconfig VLANS_PORT${portStr}_PRIO"
	echo -e "\tint \"Priority\""
	echo -e "\tdefault -1"
	echo -e "\trange -1 7"
	echo -e "\thelp"
	echo -e "\t Priority value used when tagging frames or to override priority passed"
	echo -e "\t to RTU."
	echo -e "\t -1 disables the priority overwrite. Valid values are from -1 to 7."

	echo -e "\nconfig VLANS_PORT${portStr}_VID"
	echo -e "\tstring \"VID\""
	echo -e "\tdefault \"\""
	help_vlan_port_vid
	
	echo -e "\nconfig VLANS_PORT${portStr}_PTP_VID"
	echo -e "\tstring \"PTP VID\""
	echo -e "\tdepends on VLANS_RAW_PORT_CONFIG"
	echo -e "\tdefault VLANS_PORT${portStr}_VID"
	echo -e "\thelp"
	echo -e "\t VID used for the PTP messages"
	
}

function help_vlan_port_mode() {
	echo -e "\thelp"
	echo -e "\t  In terms of VLAN-tag, there are four types of VLAN-tags that can"
	echo -e "\t  extend the Ethernet Frame header:"
	echo -e "\t  * none     - tag is not included in the Ethernet Frame"
	echo -e "\t  * priority - tag that has VID=0x0"
	echo -e "\t  * VLAN     - tag that has VID in the range 0x001 to 0xFFE"
	echo -e "\t  * null     - tag that has VID=0xFFF"
	echo -e "\n\t  The behaviour of each PMODE that can be set per-port depends on the"
	echo -e "\t  type of VLAN-tag in the received frame."
	echo -e "\n\t  - PMODE=access (0x0), frames with:"
	echo -e "\t    * no VLAN-tag  : are admitted, tagged with the values of VID and"
	echo -e "\t                     priority that are configured in PORT_VID and"
	echo -e "\t                     PRIO_VAL respectively"
	echo -e "\t    * priority tag : are admitted, their tag is unchanged, the value of"
	echo -e "\t                     VID provided to the RTU is overridden with the"
	echo -e "\t                     configured in PORT_VID. If PRIO_VAL is not -1,"
	echo -e "\t                     the value of priority provided to RTU is"
	echo -e "\t                     overridden with the configured PRIO_VAL"
	echo -e "\t    * VLAN tag     : are discarded"
	echo -e "\t    * null tag     : are discarded"
	echo -e "\n\t  - PMODE=trunk (0x1), frames with:"
	echo -e "\t    * no VLAN-tag  : are discarded"
	echo -e "\t    * priority tag : are discarded"
	echo -e "\t    * VLAN tag     : are admitted; if PRIO_VAL is not -1, the value of"
	echo -e "\t                     priority provided to RTU is overridden with"
	echo -e "\t                     the configured PRIO_VAL"
	echo -e "\t    * null tag     : are discarded"
	echo -e "\n\t  - PMODE=disabled (0x2), frames with:"
	echo -e "\t    * no VLAN-tag  : are admitted. No other configuration is used even"
	echo -e "\t                     if set."
	echo -e "\t    * priority tag : are admitted; if PRIO_VAL is not -1, the value of"
	echo -e "\t                     priority provided to RTU is overridden with"
	echo -e "\t                     the configured PRIO_VAL"
	echo -e "\t    * VLAN tag     : are admitted; if PRIO_VAL is not -1, the value of"
	echo -e "\t                     priority provided to RTU is overridden with"
	echo -e "\t                     the configured PRIO_VAL"
	echo -e "\t    * null tag     : are admitted; if PRIO_VAL is not -1, the value of"
	echo -e "\t                     priority provided to RTU is overridden with"
	echo -e "\t                     the configured PRIO_VAL"
	echo -e "\n\t  - PMODE=unqualified (0x3), frames with:"
	echo -e "\t    * no VLAN-tag  : are admitted. No other configuration is used even"
	echo -e "\t                     if set."
	echo -e "\t    * priority tag : are admitted. Their tag is unchanged, the value of"
	echo -e "\t                     VID provided to the RTU is overridden with the"
	echo -e "\t                     configured in PORT_VID. If PRIO_VAL is not -1,"
	echo -e "\t                     the value of priority provided to RTU is"
	echo -e "\t                     overridden with the configured PRIO_VAL"
	echo -e "\t                     NOTE: providing a VID for this mode is not"
	echo -e "\t                     supported in the dot-config"
	echo -e "\t    * VLAN tag     : are admitted; if PRIO_VAL is not -1, the value of"
	echo -e "\t                     priority provided to RTU is overridden with"
	echo -e "\t                     the configured PRIO_VAL"
	echo -e "\t    * null tag     : discarded."
} 

function help_vlan_port_vid() {
	echo -e "\thelp"
	echo -e "\t  This value based on the port's mode is used as:"
	echo -e "\t  --MODE_ACCESS - (mandatory) use as VID for tagging incoming frames and notify"
	echo -e "\t    the PPSI which VLAN shall it use for synchronization; only one VLAN"
	echo -e "\t    number shall be used in this mode"
	echo -e "\t  --MODE_TRUNK - (optional) notify the PPSI which VLAN shall it use for"
	echo -e "\t    synchronization; semicolon separated list is allowed"
	echo -e "\t  --MODE_DISABLED - (optional) notify the PPSI which VLANs shall it use for"
	echo -e "\t    synchronization; semicolon separated list is allowed"
	echo -e "\t  --MODE_UNQUALIFIED - (optional) notify the PPSI which VLANs shall it use for"
	echo -e "\t    synchronization; semicolon separated list is allowed;"
	echo -e "\t  The range of a valid VID is 0 to 4094"
} 

function print_vlans_header () {
	echo -e "\nmenu \"VLANs configuration\""
	echo -e "\t depends on VLANS_ENABLE"

}

function print_vlans_footer () {
	echo -e "\nendmenu" 
}

function print_vlan_set_header() {
	local vset=$1 
	local idx1=$2
	local idx2=$3

	echo -e "\nconfig VLANS_ENABLE_SET$vset"
	echo -e "\tbool \"Enable configuration for VLANs $idx1-$idx2\""
	echo -e "\tdefault n"
	echo -e "\thelp"
	echo -e "\nmenu \"Configuration for VLANs $idx1-$idx2\""
	echo -e "\tdepends on VLANS_ENABLE_SET$vset"
}

function print_vlan_set_footer() {	
	local vset=$1 
	local idx1=$2
	local idx2=$3
	echo -e "endmenu #nmenu \"Configuration for VLANs $idx1-$idx2\""
}

function print_vlan_set_config() {
	local vset=$1 
	local idx1=$2
	local idx2=$3
	local idx=$4
	local vlanStr=`printf "%04d" $idx`
	
	echo -e "\nconfig VLANS_VLAN${vlanStr}"
	echo -e "\tstring \"VLAN${idx} configuration\""
	echo -e "\tdefault \"\""
	echo -e "\thelp"
	echo -e "\t Provide the configuration for VLAN${idx}"
	echo -e "\t Example:"
	echo -e "\t  fid=0,prio=4,drop=no,ports=1;2;3-5;7"
	echo -e "\t  Where:"
	echo -e "\t  --\"fid\" is a associated Filtering ID (FID) number. One FID can be"
	echo -e "\t     associated with many VIDs. RTU learning is performed per-FID."
	echo -e "\t     Associating many VIDs with a single FID allowed shared-VLANs"
	echo -e "\t     learning."
	echo -e "\t  --\"prio\" is a priority of a VLAN; can take values between -1 and 7"
	echo -e "\t     -1 disables priority override, any other valid value takes"
	echo -e "\t     precedence over port priority"
	echo -e "\t  --If \"drop\" is set to y, yes or 1 all frames belonging to this VID are"
	echo -e "\t     dropped (note that frame can belong to a VID as a consequence of"
	echo -e "\t     per-port Endpoint configuration); can take values y, yes, 1, n, no, 0"
	echo -e "\t  --\"ports\" is a list of ports separated with a semicolon sign (\";\");"
	echo -e "\t     ports ranges are supported (with a minus sign)"
}

function generate_configuration () {
	print_header
	print_ports_header
	for p in `seq 1 $portCount`; do
		print_port_header ${p}
		print_port_config ${p}	
		print_port_footer ${p}
	done
	print_ports_footer
	
	print_vlans_header
	vlanSet=1
	for s in "${vlan_sets[@]}" ; do
		idx1=`echo "$s" | grep -o -e "^[0-9]*" `
		idx2=`echo "$s" | grep -o -e "[0-9]*$" `
		print_vlan_set_header $vlanSet $idx1 $idx2
		for idx in `seq $idx1 $idx2`; do
			print_vlan_set_config $vlanSet $idx1 $idx2 $idx
		done
		print_vlan_set_footer $vlanSet $idx1 $idx2
		vlanSet=$(($vlanSet+1))
	done 
	print_vlans_footer
	
	print_footer
} >$OUTPUT_FILE

# Generation parameters
portCount=18
vlan_sets=("0:23" "23:100" "101:4094")
generate_configuration


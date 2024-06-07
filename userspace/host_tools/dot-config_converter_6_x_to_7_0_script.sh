#!/bin/bash

# Author: Adam Wujek 2024

# This script converts dot-config used in WRS firmware 6.x to WRS firmware 7.0

# Script flow:
# - source dot-config
# - check for old parameters to be replaced, add new parameters at the end of dot-config
# - pass the new dot-config via conf tool to place all parameters in the correct order and add still missing default values
#
# This scripts adds to CONFIG_DOTCONF_INFO the information that dot-config was converted

showHelp() {
cat << EOF
This script converts dot-config used with WRS firmware v6.x to dot-config for WRS firmware v7.0

Usage: $script_name -i <old_dot-config> -o <new_dot-config>


EOF
}

portCount=18
instCount=1

script_name=$0

V=false

if [ -z "$CONF_PATH" ] || [ -z "$KCONFIG_PATH" ]; then
    echo "CONF_PATH or KCONFIG_PATH is not set. Please use a wrapper to run this script."
    exit 1
fi

while true
do
    case $1 in
    -h|--help)
	showHelp $script_name
	exit 0
	;;
    -i|--input)
	shift
	input_file="$1"
	;;
    -o|--output)
	shift
	output_file="$1"
	;;
    -v|--verbose)
	shift
	V=true
	;;
    "")
	break
	;;
    *)
	echo "Unrecognized option $1"
	exit 1
    esac
    shift
done

$V && echo "Input file is \"$input_file\""
$V && echo "Output file is \"$output_file\""


if [ x"$input_file" == x ] || ! [ -f "$input_file" ] || [ -d "$input_file" ]; then
    echo "Please provide input file."
    exit 1
fi

if [ x"$output_file" == x ]; then
    echo "Please provide output file."
    exit 1
fi


source "$input_file"

cp "$input_file" "$output_file"
# rm "$output_file"

$V && echo "Write new version of FW in dot-config (CONFIG_DOTCONF_FW_VERSION)"
echo "CONFIG_DOTCONF_FW_VERSION=\"7.0\"" >> "$output_file"

$V && echo "Add info about conversion from old ($CONFIG_DOTCONF_FW_VERSION) firmware"
if [ -n "$CONFIG_DOTCONF_INFO" ]; then
    CONFIG_DOTCONF_INFO="$CONFIG_DOTCONF_INFO; "
fi
echo "CONFIG_DOTCONF_INFO=\"${CONFIG_DOTCONF_INFO}Dot-config converted from $CONFIG_DOTCONF_FW_VERSION\"" >> "$output_file"

# add Global profile as HA
$V && echo "Add CONFIG_GLOBAL_PROFILE_HA_WR=y"
echo "CONFIG_GLOBAL_PROFILE_HA_WR=y" >> "$output_file"

# Check BMCA
if [ "$CONFIG_PTP_OPT_EXT_PORT_CONFIG_ENABLED" == "y" ]; then
    $V && echo "External port configuration enabled, add CONFIG_PTP_OPT_BMCA_EXT_PORT_CONFIG=y"
    echo "CONFIG_PTP_OPT_BMCA_EXT_PORT_CONFIG=y" >> "$output_file"
else
    $V && echo "External port configuration not enabled, add CONFIG_PTP_OPT_BMCA_STANDARD=y"
    echo "CONFIG_PTP_OPT_BMCA_STANDARD=y" >> "$output_file"
fi

if [ -n "$CONFIG_PTP_OPT_DOMAIN_NUMBER" ] && [ "$CONFIG_PTP_OPT_DOMAIN_NUMBER" != 0 ]; then
    $V && echo "Non default value for CONFIG_PTP_OPT_DOMAIN_NUMBER, add CONFIG_PTP_OPT_OVERWRITE_ATTRIBUTES=y"
    set_PTP_OPT_OVERWRITE_ATTRIBUTES=y
fi

if [ -n "$CONFIG_PTP_OPT_PRIORITY1" ] && [ "$CONFIG_PTP_OPT_PRIORITY1" != 128 ]; then
    $V && echo "Non default value for CONFIG_PTP_OPT_PRIORITY1, add CONFIG_PTP_OPT_OVERWRITE_ATTRIBUTES=y"
    set_PTP_OPT_OVERWRITE_ATTRIBUTES=y
fi

if [ -n "$CONFIG_PTP_OPT_PRIORITY2" ] && [ "$CONFIG_PTP_OPT_PRIORITY2" != 128 ]; then
    $V && echo "Non default value for CONFIG_PTP_OPT_PRIORITY2, add CONFIG_PTP_OPT_OVERWRITE_ATTRIBUTES=y"
    set_PTP_OPT_OVERWRITE_ATTRIBUTES=y
fi

if [ "$set_PTP_OPT_OVERWRITE_ATTRIBUTES" = y ]; then
     echo "CONFIG_PTP_OPT_OVERWRITE_ATTRIBUTES=y" >> "$output_file"
fi


# Per port per instance checks
for port in `seq 1 $portCount`; do
    portStr=`printf "%02d" $port`
    for inst in `seq 1 $instCount`; do
	instStr=`printf "%02d" $inst`

	# Add CONFIG_PORTXX_INSTYY_PROFILE_KEEP_GLOBAL=y
	echo "CONFIG_PORT${portStr}_INST${instStr}_PROFILE_KEEP_GLOBAL=y" >> "$output_file"

	# Check old profile
	varname="CONFIG_PORT${portStr}_INST${instStr}_PROFILE_PTP"
	if [ "${!varname}" = "y" ]; then
	    # Add extension none (CONFIG_PORTXX_INSTYY_EXTENSION_NONE=y)
	    echo "CONFIG_PORT${portStr}_INST${instStr}_EXTENSION_NONE=y" >> "$output_file"
	else
	    # Add extension WR (CONFIG_PORTXX_INSTYY_EXTENSION_WR=y)
	    echo "CONFIG_PORT${portStr}_INST${instStr}_EXTENSION_WR=y" >> "$output_file"
	fi

	# Check if announce interval contains not default value
	varname="CONFIG_PORT${portStr}_INST${instStr}_ANNOUNCE_INTERVAL"
	if [ -n "${!varname}" ] && [ "${!varname}" != "1" ]; then
	    $V && echo "Non default value for ${varname} found(${!varname}), add ${varname}_OVERWRITE=y"
	    echo "${varname}_OVERWRITE=y" >> "$output_file"
	fi

	# Check if receipt timeout contains not default value
	varname="CONFIG_PORT${portStr}_INST${instStr}_ANNOUNCE_RECEIPT_TIMEOUT"
	if [ -n "${!varname}" ] && [ "${!varname}" != "3" ]; then
	    $V && echo "Non default value for ${varname} found(${!varname}), add ${varname}_OVERWRITE=y"
	    echo "${varname}_OVERWRITE=y" >> "$output_file"
	fi

	# Check if min delay request interval contains not default value
	varname="CONFIG_PORT${portStr}_INST${instStr}_MIN_DELAY_REQ_INTERVAL"
	if [ -n "${!varname}" ] && [ "${!varname}" != "0" ]; then
	    $V && echo "Non default value for ${varname} found(${!varname}), add ${varname}_OVERWRITE=y"
	    echo "${varname}_OVERWRITE=y" >> "$output_file"
	fi

	# Check if min pdelay request interval contains not default value
	varname="CONFIG_PORT${portStr}_INST${instStr}_MIN_PDELAY_REQ_INTERVAL"
	if [ -n "${!varname}" ] && [ "${!varname}" != "0" ]; then
	    $V && echo "Non default value for ${varname} found(${!varname}), add ${varname}_OVERWRITE=y"
	    echo "${varname}_OVERWRITE=y" >> "$output_file"
	fi

	# Check if sync interval contains not default value
	varname="CONFIG_PORT${portStr}_INST${instStr}_SYNC_INTERVAL"
	if [ -n "${!varname}" ] && [ "${!varname}" != "0" ]; then
	    $V && echo "Non default value for ${varname} found(${!varname}), add ${varname}_OVERWRITE=y"
	    echo "${varname}_OVERWRITE=y" >> "$output_file"
	fi

    done
    echo "" >> "$output_file"
done

output_file="$(realpath $output_file)"
CONF_PATH="$(realpath $CONF_PATH)/"

# conf has to be called from the path where Kconfig files are placed.
# Otherwise include directive in Kconfig* is not able to find files.
cd "$KCONFIG_PATH"

$V && echo "New configuration items:"
$V && KCONFIG_CONFIG="$output_file" "${CONF_PATH}"conf --listnewconfig Kconfig

$V && echo "Overwriten configuration items:"
$V && KCONFIG_CONFIG="$output_file" "${CONF_PATH}"conf  -s --olddefconfig Kconfig
$V || KCONFIG_CONFIG="$output_file" "${CONF_PATH}"conf  -s --olddefconfig Kconfig &> /dev/null

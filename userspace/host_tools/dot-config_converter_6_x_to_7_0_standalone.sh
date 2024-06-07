#!/bin/bash

# Author: Adam Wujek 2024

# This is a wrapper for the script that converts dot-config used in
# WRS firmware 6.x to WRS firmware 7.0


script_name=$0
SCRIPT_PATH=$(dirname "$0")

CONF_PATH=${SCRIPT_PATH}/bin/ KCONFIG_PATH=${SCRIPT_PATH}/kconfigs/v7.0/ ${SCRIPT_PATH}/bin/dot-config_converter_6_x_to_7_0_script.sh $@

#!/bin/bash

# Author: Adam Wujek 2024

# This is a wrapper for the script that converts dot-config used in
# WRS firmware 6.x to WRS firmware 7.0


script_name=$0
SCRIPT_PATH=$(dirname "$0")

CONF_PATH=${SCRIPT_PATH}/../../scripts/kconfig/ KCONFIG_PATH=${SCRIPT_PATH}/../../ ${SCRIPT_PATH}/dot-config_converter_6_x_to_7_0_script.sh $@

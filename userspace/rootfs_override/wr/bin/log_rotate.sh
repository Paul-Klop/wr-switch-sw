#!/bin/bash

#############################################################
# Maciej Lipinski @CERN
#
# Script to rotate files written by rsyslogd, it is called
# by rsyslog as per configuration in /etc/rsyslog.conf 
# (more info in this config file)
#
# Parameters:
# 1. Path and name of the output file that is written by
#    rsyslogd
# 2. Number of rotate files (copies of the file written 
#    by rsyslogd
#
# Example usage:
# log_rotate.sh /tmp/log_rotation.log 3
#############################################################

# Set log file name, from input or default
if [ -z $1 ]; then
  log_file=/tmp/log_rotation.log
else
  log_file=$1 
fi

# Set number of rotated logs, fron input or default
if [ -z $2 ]; then
  log_numb=10
elif [ $2 -lt 2 ]; then
  log_numb=2
else
  log_numb=$2
fi

echo "Rotate log file $log_file $log_numb times"

# Just in case, check whether the log file exists
if [ ! -f "$log_file" ]; then
  echo "No logfile $log_file"
  exit 1;
fi

# Rotate old syslog files, indexed by numbers (higher values
# for older files). Numbers are used, not the date, because
# the date can change, e.g. as a result of PTP synchronization.
# In this process, we discard files with max-specified number
# (ie.e. $log_num).
i=$log_numb;
while [ $i -gt 1 ]; do
  printf -v new_rotate_file "%s-%02d" $log_file $i;
  i=$(($i - 1))
  printf -v old_rotate_file "%s-%02d" $log_file $i;
  echo "move $old_rotate_file to $new_rotate_file"
  # Discard any errors when using mv, errors happen at the begining
  # when there is no rotated files.
  mv -f $old_rotate_file $new_rotate_file 2> /dev/null
done

# move the syslog file as to the rotated files with the lowest 
# index because it is the newest
new_rotate_file=${log_file}-01
mv -f $log_file $new_rotate_file
echo "Move $log_file to $new_rotate_file"

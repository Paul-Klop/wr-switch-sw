#!/bin/sh

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
else
  log_numb=$2
fi

echo "Rotate log file $log_file $log_numb times"

# Just in case, check whether the log file exists 
if [ ! -f "$log_file" ]; then
  echo "No logfile $log_file"
  exit 1;
fi

# Move the log file from rsyslogd into the rotate file
# The date is an integer so that it is easy to recongize
# the oldest file.

new_rotate_file=${log_file}-$(date +%Y%m%d%H%M%S)
mv -f $log_file $new_rotate_file
echo "Move $log_file to $new_rotate_file"

# Remove excess rotate file. Just in case, there are
# more than a single excess rotate file, remove excess
# files until happy with its number.

# Print the expected and current number of rotate files (just for info)
log_cnt=$(/bin/ls ${log_file}-* | /usr/bin/wc -l)
echo "Number of rotate files is $log_cnt and should be max $log_numb"

# Remove any excess files
while [ 1 ]; do

  # Check the number of rotate files.
  log_cnt=$(/bin/ls ${log_file}-* | /usr/bin/wc -l)

  if [ $log_cnt -gt $log_numb ]; then
    # Remove the oldest file if there are too many rotate files.
    oldest_rotate_file=$(/bin/ls ${log_file}-* | /usr/bin/head -1)
    echo "Remove oldest file: $oldest_rotate_file"
    rm $oldest_rotate_file
  else
    break
  fi
done


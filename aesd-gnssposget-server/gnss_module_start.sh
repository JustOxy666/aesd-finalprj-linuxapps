#!/bin/bash

# This script will configure uBlox NE 6M
# for efficient GNSS data reception

data_available=0
sleepS_time=2
sleepMs_time=200
tty_device=/dev/ttyAMA1 # UART3

# ---------------------------------------------
# Setup port with default baudrate 9600
chmod 777 $tty_device
stty -F $tty_device 9600 cs8 -cstopb -parenb -ixon -echo
sleep $sleepS_time

# ---------------------------------------------
# Wait until GNSS module outputs data
while [ $data_available -eq 0 ]
do
    read -r -t $sleepS_time data < $tty_device
    echo "data=$data"
    if [ "$data" = "" ]; then
        echo "Waiting $sleepS_time seconds before new attempt"
        sleep $sleepS_time
    else
        data_available=1
    fi
done

# ---------------------------------------------
# Disable extra NMEA messages processing only RMC & GSV
# Disable GSA
printf "\xb5\x62\x06\x01\x03\x00\xF0\x02\x00\xFC\x13" > /dev/ttyAMA1
usleep $sleepMs_time

# Disable GGA
printf "\xb5\x62\x06\x01\x03\x00\xF0\x00\x00\xFA\x0F" > /dev/ttyAMA1
usleep $sleepMs_time

# Disable VTG
printf "\xb5\x62\x06\x01\x03\x00\xF0\x05\x00\xFF\x19" > /dev/ttyAMA1
usleep $sleepMs_time

# Disable GLL
printf "\xb5\x62\x06\x01\x03\x00\xF0\x01\x00\xFB\x11" > /dev/ttyAMA1
usleep $sleepMs_time

# ---------------------------------------------
# Set baudrate to 19200
printf "\xb5\x62\x06\x00\x14\x00\x01\x00\x00\x00\xC0\x08\x00\x00\x00\x4B\x00\x00\x27\x00\x23\x00\x00\x00\x00\x00\x78\x17" > /dev/ttyAMA1
usleep $sleepMs_time
stty -F $tty_device 19200 cs8 -cstopb -parenb -echo
sleep $sleepS_time

# Set 5Hz data output
printf "\xB5\x62\x06\x08\x06\x00\xC8\x00\x01\x00\x01\x00\xDE\x6A" > /dev/ttyAMA1
usleep $sleepMs_time

exit 0

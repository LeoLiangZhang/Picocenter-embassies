#!/bin/bash

for i in "$@"
do
	line=$(sudo cat /boot/System.map-`uname -r` |grep -E "\b$i\b")
	if [ -n "$line" ] ; then
		addr=`expr "$line" : '\([^ ]*\)'`
		echo "#define SYSMAP_$i 0x$addr" 
	else
		exit 1
	fi
done
exit

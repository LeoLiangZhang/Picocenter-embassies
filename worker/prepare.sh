#!/bin/bash

# This script is used to prepare environment for KVM-based picoprocess. 

ROOT_DIR=/elasticity/embassies
KVM_MON_DIR=$ROOT_DIR/monitors/linux_kvm
UVMEM_DIR=$KVM_MON_DIR/monitor/uvmem
WORKER_DIR=$ROOT_DIR/worker
EMBASSIES_IPTABLES_FILE=embassies_iptables.conf


echo 'Build and install uvmem'
(cd $UVMEM_DIR ; make clean ; make ; make insmod)
echo

echo 'Build iptables_helper'
schroot -d $WORKER_DIR/iptables_helper -- make
echo

if [ ! -f /etc/rsyslog.d ]; then
	echo 'Create symbolic link to' $EMBASSIES_IPTABLES_FILE
	sudo ln -s $WORKER_DIR/$EMBASSIES_IPTABLES_FILE \
	           /etc/rsyslog.d/$EMBASSIES_IPTABLES_FILE
fi

echo 'List dependencies'
ls -l /dev/uvmem
ls -l $WORKER_DIR/iptables_helper/build/iptables_helper
ls -l /etc/rsyslog.d/$EMBASSIES_IPTABLES_FILE
echo "DONE"

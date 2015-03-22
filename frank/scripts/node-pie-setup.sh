#!/bin/bash

# MUST BE RUN WITHIN SCHROOT ENVIRONMENT! 

# Builds node.js v0.12 as a position-independent executable, 
# assuming picocenter is setup in the /elasticity/embassies/ 
# directory

cd /elasticity/embassies/toolchains/linux_elf/

# Create new apps folder
mkdir -p frank_apps/nodejs
cd frank_apps/nodejs

# Grab source (may need to replace v0.12.0 with latest)
wget http://nodejs.org/dist/v0.12.0/node-v0.12.0.tar.gz
tar -xf node-v0.12.0.tar.gz
cd node-v0.12.0

# Configure doesn't expose any options for making pie, so nothing fancy here
./configure 

# Actual makefiles are located in the out/ directory
cd out/

# Add -fPIC to all CC options and -pie to all LD options 
sed -i '/.*\(C\|CXX\)FLAGS\.\(target\|host\).*/ s/$/ -fPIC/' Makefile
sed -i '/.*LDFLAGS\.\(target\|host\).*/ s/$/ -pie' Makefile
sed -i '/.*CFLAGS_\(Debug\|Release\).*/a \\t-fPIC \\' node.target.mk
sed -i '/.*LDFLAGS_\(Debug\|Release\).*/a \\t-pie \\' node.target.mk

# WARNING.. make -j might cause you to exhaust virtual memory if 
# you're not using a swapfile, so either add one or simply 
# kill it and rerun make or make -j2 
cd ..
make -j
make install

# Check if it worked 
if readelf -e node | grep PHDR | awk '{print $3}' | grep -q 0x0000; then
    echo "You got a PIE! (It worked.)"
else
    echo "Hm, you might not have created the PIE successfully.. check manually with readelf -e node"
fi

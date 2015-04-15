# Prepare environment

* Add git repo ssh config to `~/.ssh/config`.

  ```
Host gluon
     HostName quark.ccs.neu.edu
     Port 2224
```

* Make dir `/elasticity`
  + `sudo mkdir /elasticity`
  + Recommend to create a group `elasticity`, and make it the folder's group owner
  + `chmod g+rws /elasticity`
  + Use the group to share the setup.

* Clone source to `/elasticity/embassies`

  ```
cd /elasticity
git clone git@gluon:liang/embassies.git
```

# Install schroot'ed Debian Squeeze on Ubuntu 14.04

0. Install these libs

  ```
sudo apt-get install schroot debootstrap
```

1. Make chroot folder `/elasticity/chroot/squeeze_i386`

  ```
mkdir -p /elasticity/chroot/squeeze_i386
```

2. Create a file at `/etc/schroot/chroot.d/squeeze_i386.conf` with

  ```
[squeeze_i386]
aliases=squeeze,picoprocess,default
description=Debian 6.0 Squeeze for i386
type=directory
directory=/elasticity/chroot/squeeze_i386
personality=linux32
root-users=liang
users=liang
groups=sudo,elasticity
```

4. Install Debian Squeeze (ver.6) with debootstrap

  ```
sudo debootstrap --variant=buildd --arch=i386 squeeze /elasticity/chroot/squeeze_i386 http://ftp.debian.org/debian/
```

5. Mount `/elasticity/embassies` in schroot

  ```
sudo bash -c 'echo "/elasticity/embassies /elasticity/embassies none    rw,bind     0   0" >> /etc/schroot/default/fstab'
```

  Here is a sample:
  ```
$ cat /etc/schroot/default/fstab
# fstab: static file system information for chroots.
# Note that the mount point will be prefixed by the chroot path
# (CHROOT_PATH)
#
# <file system> <mount point>   <type>  <options>       <dump>  <pass>
/proc           /proc           none    rw,bind         0       0
/sys            /sys            none    rw,bind         0       0
/dev            /dev            none    rw,bind         0       0
/dev/pts        /dev/pts        none    rw,bind         0       0
/home           /home           none    rw,bind         0       0
/tmp            /tmp            none    rw,bind         0       0

# It may be desirable to have access to /run, especially if you wish
# to run additional services in the chroot.  However, note that this
# may potentially cause undesirable behaviour on upgrades, such as
# killing services on the host.
#/run           /run            none    rw,bind         0       0
#/run/lock      /run/lock       none    rw,bind         0       0
#/dev/shm       /dev/shm        none    rw,bind         0       0
#/run/shm       /run/shm        none    rw,bind         0       0
/elasticity/embassies /elasticity/embassies none    rw,bind     0   0
```

# Setup Debian Squeeze environment for PicoCenter

1. Append apt source entries in schroot

  ```
# Add these to /etc/apt/sources.list
deb-src http://ftp.debian.org/debian squeeze main
deb http://ftp.debian.org/debian squeeze non-free
```

  or run scripts:

  ```
echo 'deb-src http://ftp.debian.org/debian squeeze main' |sudo tee -a /elasticity/chroot/squeeze_i386/etc/apt/sources.list
echo 'deb http://ftp.debian.org/debian squeeze non-free' |sudo tee -a /elasticity/chroot/squeeze_i386/etc/apt/sources.list
```

2. Install Debian keyrings

  ```
sudo schroot -- apt-get -y --force-yes install debian-keyring debian-ports-archive-keyring debian-edu-archive-keyring
sudo schroot -- apt-get update  # need update to make the keyring active
```

3. Install depend libraries

  ```
sudo schroot -- apt-get -y --force-yes install \
python python2.6-dev python-pip python-profiler \
wget unzip fakeroot bc sudo \
xorg xorg-dev libcap-dev libpng12-dev libgtk2.0-bin libgtk2.0-dev \
libdb-dev libssl0.9.8 zlib1g-dev libssl-dev libdisasm-dev libcap2 libnetpbm10-dev \
libcap2-bin \
m4 gdb libdisasm0 \
net-tools procps iptables curl \
bash-completion emacs
```

  and a few more python packets via pip:
  ```
sudo schroot -- pip install ordereddict boto pyzmq tornado line_profiler msgpack-python
```

4. Setup timezone (optional)

  ```
sudo schroot -- dpkg-reconfigure tzdata
```

# Compile Embassies

  ```
schroot -d /elasticity/embassies -- make
schroot -d /elasticity/embassies/toolchains/linux_elf/zftp_backend -- make
schroot -d /elasticity/embassies/toolchains/linux_elf/zguest -- make
```


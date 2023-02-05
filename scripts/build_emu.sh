#!/bin/bash

# Install dependencies
apt install -y gcc git libglib2.0-dev libfdt-dev libpixman-1-dev zlib1g-dev ninja-build
apt install -y libnfs-dev libiscsi-dev

apt install -y libaio-dev libbluetooth-dev libcapstone-dev libbrlapi-dev libbz2-dev
apt install -y libcap-ng-dev libcurl4-gnutls-dev libgtk-3-dev
apt install -y libibverbs-dev libjpeg8-dev libncurses5-dev libnuma-dev
apt install -y librbd-dev librdmacm-dev
apt install -y libsasl2-dev libsdl2-dev libseccomp-dev libsnappy-dev libssh-dev
apt install -y libvde-dev libvdeplug-dev libvte-2.91-dev libxen-dev liblzo2-dev
apt install -y valgrind xfslibs-dev

ROOT_DIR=$(realpath $(dirname $(dirname $0)))
. ${ROOT_DIR}/scripts/config

# Get source code
cd ${ROOT_DIR}
[ ! -d ${ROOT_DIR}/emulator ] && mkdir emulator
cd emulator
if [ ! -d ${ROOT_DIR}/emulator/qemu-${QEMU_VERSION} ]; then
	wget https://download.qemu-project.org/qemu-${QEMU_VERSION}.tar.xz
	tar xvJf qemu-${QEMU_VERSION}.tar.xz
	rm qemu-${QEMU_VERSION}.tar.xz
fi

# Build
cd qemu-${QEMU_VERSION}
[ ! -d ${ROOT_DIR}/emulator/qemu-${QEMU_VERSION}/build ] && mkdir build
cd build
if [ ! -f ${ROOT_DIR}/emulator/qemu-${QEMU_VERSION}/build/qemu-system-x86_64 ]; then
    ../configure
    make -j$(($(nproc)-1))
fi

# Check
echo "Done"
${ROOT_DIR}/emulator/qemu-${QEMU_VERSION}/build/qemu-system-x86_64 -version

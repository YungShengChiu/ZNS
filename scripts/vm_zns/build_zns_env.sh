#!/bin/bash

# Install dependencies and tools
apt install -y gcc git fio make meson
apt install -y util-linux lsscsi nvme-cli sg3-utils
apt install -y libblkid1 libblkid-dev libzbd2 libzbd-dev zbd-utils libnvme-dev

# Download submodule
git submodule update --init

ROOT_DIR=$(realpath $(dirname $(dirname $(dirname $0))))
. ${ROOT_DIR}/scripts/vm_zns/config

# Install SPDK
cd ${ROOT_DIR}/spdk
git checkout v${SPDK_VERSION}
git submodule update --init
scripts/pkgdep.sh --all
./configure
make -j$(($(nproc)-1))
./test/unit/unittest.sh
make install

# Verify ZNS
echo "Listing all the NVMe devices"
nvme list
. ${ROOT_DIR}/scripts/vm_zns/verify_zns_disk.sh

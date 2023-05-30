#!/bin/bash

# Install dependencies and tools
apt install -y gcc git vim fio make meson
apt install -y util-linux lsscsi nvme-cli sg3-utils
apt install -y libblkid1 libblkid-dev libzbd2 libzbd-dev zbd-utils libnvme-dev

ROOT_DIR=$(realpath $(dirname $(dirname $(dirname $0))))
. ${ROOT_DIR}/scripts/vm_zns/config

# Download and install SPDK libraries
if [ ! -d ${ROOT_DIR}/spdk ]; then
    git clone https://github.com/spdk/spdk.git
    cd ${ROOT_DIR}/spdk
    git submodule update --init
    scripts/pkgdep.sh --all
    git checkout v${SPDK_VERSION}
    scripts/pkgdep.sh --all
    ./configure
    make -j$(nproc)
    ./test/unit/unittest.sh
    make install
fi

# Verify ZNS
echo "Listing all the NVMe devices"
nvme list
. ${ROOT_DIR}/scripts/vm_zns/verify_zns_disk.sh

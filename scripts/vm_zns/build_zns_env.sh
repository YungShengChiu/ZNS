#!/bin/bash

# Install dependencies and tools
apt install -y gcc git fio make
apt install -y util-linux lsscsi nvme-cli sg3-utils
apt install -y libblkid1 libblkid-dev libzbd2 libzbd-dev zbd-utils

ROOT_DIR=$(realpath $(dirname $(dirname $(dirname $0))))

# Verify ZNS
echo "Listing all the NVMe devices"
nvme list
. ${ROOT_DIR}/scripts/vm_zns/verify_zns_disk.sh

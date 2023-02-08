#!/bin/bash

# Install dependices and tools
apt install -y gcc git fio
apt install -y util-linux lsscsi nvme-cli sg3-utils
apt install -y libblkid1 libblkid-dev libzbd2 libzbd-dev zbd-utils

# Verify ZNS
nvme list

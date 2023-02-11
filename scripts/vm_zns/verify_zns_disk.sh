#!/bin/bash


ROOT_DIR=$(realpath $(dirname $(dirname $(dirname $0))))
. ${ROOT_DIR}/scripts/vm_zns/config

# Display the kernel configuration
echo "\nKernel configuration of zoned block device support"
cat /boot/config-$(uname -r) | grep CONFIG_BLK_DEV_ZONED

# Display the I/O scheduler of the ZNS disk
echo "\nI/O scheduler of ${ZNS_NAME}"
cat /sys/block/${ZNS_NAME}/queue/scheduler

# Test ZNS operation
nvme zns -version
nvme zns id-ns /dev/${ZNS_NAME} -H
echo "\nTest ${ZNS_NAME} operation"
echo "\nReset zone 0"
nvme zns reset-zone /dev/${ZNS_NAME} -s 0x0
echo "\nReport Zone 0"
nvme zns report-zones /dev/${ZNS_NAME} -d 1
echo "\nAppend \"Zone append test\" to zone 0"
echo "Zone append test" | nvme zns zone-append /dev/${ZNS_NAME} -z 0x1000 -s 0x0
echo "\nRead the data back"
echo "The data in zone 0 LBA 0 is \"$(nvme read /dev/${ZNS_NAME} -z 0x1000 -s 0x0)\""
echo "\nReport Zone 0"
nvme zns report-zones /dev/${ZNS_NAME} -d 1
echo "\nReset zone 0"
nvme zns reset-zone /dev/${ZNS_NAME} -s 0x0
echo "\nReport Zone 0"
nvme zns report-zones /dev/${ZNS_NAME} -d 1
echo "\nTest done!"

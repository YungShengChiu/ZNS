#!/bin/bash

# Variables
HOST_SSH_PORT=22
ROOT_DIR=$(realpath $(dirname $(dirname $0)))
QEMU_VERSION=7.1.0
QEMU_DIR=${ROOT_DIR}/emulator/qemu-${QEMU_VERSION}/build
VM_FORMAT=qcow2
VM_ISO=ubuntu-20.04.5-desktop-amd64.iso
VM_IMG=ubuntu_zns.img
VM_SIZE=32G
VM_MEM=8192
VM_NPROC=$(nproc)
VM_SSH_PORT=2222
VM_VNC_PORT=2

ZNS_IMG=zns0.raw
ZNS_SIZE=32G

ZNS_IMG_VALID=false

NVME_ID=nvme0
NVME_SERIAL=deadbeef
NVME_ZONED_ZASL=5
ZNS_NVME_ID=nvmezns0
ZNS_NVME_FORMAT=raw
ZNS_NVME_NSID=1
ZNS_NVME_LOGICAL_BLK=4096
ZNS_NVME_PHYSICAL_BLK=4096
ZNS_NVME_ZONE_SIZE=64M
ZNS_NVME_ZONE_CAPACITY=62M
ZNS_NVME_MAX_OPEN=16
ZNS_NVME_MAX_ACTIVE=32
ZNS_NVME_UUID=5e40ec5f-eeb6-4317-bc5e-c919796a5f79

cd ${ROOT_DIR}

# Create a backstore file
if [ ! -f ${ROOT_DIR}/${ZNS_IMG} ]; then
	truncate -s ${ZNS_SIZE} ${ROOT_DIR}/${ZNS_IMG}
fi

# Verify the backstore file
if [ -f ${ROOT_DIR}/${ZNS_IMG} ]; then
	ZNS_IMG_VALID=true
	ls -l ${ROOT_DIR}/${ZNS_IMG}
else
	echo "ZNS image ${ZNS_IMG} not found!"
fi


# Create and running a virtual machine
if [ ! -f ${ROOT_DIR}/${VM_IMG} ]; then
	if [ -f ${ROOT_DIR}/${VM_ISO} ]; then
		echo "Creating ${VM_IMG}"
		${QEMU_DIR}/qemu-img create -f ${VM_FORMAT} ${VM_IMG} ${VM_SIZE}
		echo "You can now access ${VM_IMG} via ip address 127.0.0.1"
		echo "Port ${VM_VNC_PORT} for VNC"
		${QEMU_DIR}/qemu-system-x86_64 \
			-hda ${VM_IMG} \
			-boot d \
			-cdrom ${VM_ISO} \
			-m ${VM_MEM} \
			-smp ${VM_NPROC} \
			-cpu host \
			--enable-kvm \
			-net user \
			-net nic \
			-vnc :${VM_VNC_PORT}
	else
		echo "${VM_ISO} not found!"
	fi
elif [ ${ZNS_IMG_VALID} ]; then
	echo "Access ${VM_IMG} via ip address 127.0.0.1"
	echo "Port ${VM_VNC_PORT} for VNC"
	echo "Port ${VM_SSH_PORT} for SSH"
	${QEMU_DIR}/qemu-system-x86_64 \
		-hda ${VM_IMG} \
		-m ${VM_MEM} \
		-smp ${VM_NPROC} \
		-cpu host \
		--enable-kvm \
		-device nvme,id=${NVME_ID},serial=${NVME_SERIAL},zoned.zasl=${NVME_ZONED_ZASL} \
		-drive file=${ZNS_IMG},id=${ZNS_NVME_ID},format=${ZNS_NVME_FORMAT},if=none \
		-device nvme-ns,drive=${ZNS_NVME_ID},bus=${NVME_ID},nsid=${ZNS_NVME_NSID},logical_block_size=${ZNS_NVME_LOGICAL_BLK},physical_block_size=${ZNS_NVME_PHYSICAL_BLK},zoned=true,zoned.zone_size=${ZNS_NVME_ZONE_SIZE},zoned.zone_capacity=${ZNS_NVME_ZONE_CAPACITY},zoned.max_open=${ZNS_NVME_MAX_OPEN},zoned.max_active=${ZNS_NVME_MAX_ACTIVE},uuid=${ZNS_NVME_UUID} \
		-net user,hostfwd=tcp::${VM_SSH_PORT}-:${HOST_SSH_PORT} \
		-net nic \
		-vnc :${VM_VNC_PORT}
else
	echo "ZNS image ${ZNS_IMG} not found!"
fi


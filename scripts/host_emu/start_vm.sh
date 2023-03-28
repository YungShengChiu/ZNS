#!/bin/bash

ROOT_DIR=$(realpath $(dirname $(dirname $(dirname $0))))
. ${ROOT_DIR}/scripts/host_emu/config
cd ${ROOT_DIR}

# Create directories for these files of virtual machine
[ ! -d ${ROOT_DIR}/vm ] && mkdir vm
[ ! -d ${DISK_DIR} ] && mkdir ${DISK_DIR}
[ ! -d ${LINUX_ISO_DIR} ] && mkdir ${LINUX_ISO_DIR}
[ ! -d ${VM_IMG_DIR} ] && mkdir ${VM_IMG_DIR}

# Create a backstore file
if [ ! -f ${DISK_DIR}/${ZNS_IMG} ]; then
	truncate -s ${ZNS_SIZE} ${DISK_DIR}/${ZNS_IMG}
fi

# Verify the backstore file
if [ -f ${DISK_DIR}/${ZNS_IMG} ]; then
	ZNS_IMG_VALID=true
	ls -l ${DISK_DIR}/${ZNS_IMG}
else
	echo "ZNS image ${DISK_DIR}/${ZNS_IMG} not found!"
fi

# Create and running a virtual machine
cd ${VM_IMG_DIR}
if [ ! -f ${VM_IMG} ]; then
	if [ -f ${LINUX_ISO_DIR}/${VM_ISO} ]; then
		echo "Creating ${VM_IMG}"
		${QEMU_DIR}/qemu-img create -f ${VM_FORMAT} ${VM_IMG} ${VM_SIZE}
		echo "You can now access ${VM_IMG} via ip address 127.0.0.1"
		echo "Port ${VM_VNC_PORT} for VNC"
		${QEMU_DIR}/qemu-system-x86_64 \
			-hda ${VM_IMG} \
			-boot d \
			-cdrom ${LINUX_ISO_DIR}/${VM_ISO} \
			-m ${VM_MEM} \
			-smp ${VM_NPROC} \
			-cpu host \
			--enable-kvm \
			-net user \
			-net nic \
			-vnc :${VM_VNC_PORT}
	else
		echo "${LINUX_ISO_DIR}/${VM_ISO} not found!"
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
		-drive file=${DISK_DIR}/${ZNS_IMG},id=${ZNS_NVME_ID},format=${ZNS_NVME_FORMAT},if=none \
		-device nvme-ns,drive=${ZNS_NVME_ID},bus=${NVME_ID},nsid=${ZNS_NVME_NSID},logical_block_size=${ZNS_NVME_LOGICAL_BLK},physical_block_size=${ZNS_NVME_PHYSICAL_BLK},zoned=true,zoned.zone_size=${ZNS_NVME_ZONE_SIZE},zoned.zone_capacity=${ZNS_NVME_ZONE_CAPACITY},zoned.max_open=${ZNS_NVME_MAX_OPEN},zoned.max_active=${ZNS_NVME_MAX_ACTIVE},uuid=${ZNS_NVME_UUID} \
		-net user,hostfwd=tcp::${VM_SSH_PORT}-:${HOST_SSH_PORT} \
		-net nic \
		-vnc :${VM_VNC_PORT}
else
	echo "ZNS image ${DISK_DIR}/${ZNS_IMG} not found!"
fi


ROOT_DIR := $(realpath ../../)
SPDK_ROOT_DIR := $(ROOT_DIR)/spdk

include $(SPDK_ROOT_DIR)/mk/spdk.common.mk
include $(SPDK_ROOT_DIR)/mk/spdk.modules.mk

APP = hello_bdev

C_SRCS := hello_bdev.c

SPDK_LIB_LIST = $(ALL_MODULES_LIST) event event_bdev

include $(SPDK_ROOT_DIR)/mk/spdk.app.mk

bdevconf:
	@ echo "Generating bdev configuration file."
	$(SPDK_ROOT_DIR)/scripts/gen_nvme.sh --json-with-subsystems > ./bdev.json
	@ echo "Configuration file generated."

bdev: all bdevconf
	sudo ./hello_bdev -c ./bdev.json

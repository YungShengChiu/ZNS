ROOT_DIR ?= $(realpath ../..)
SPDK_ROOT_DIR ?= $(ROOT_DIR)/spdk
NVME_DIR ?= $(SPDK_ROOT_DIR)/lib/nvme

include $(SPDK_ROOT_DIR)/mk/spdk.common.mk
include $(SPDK_ROOT_DIR)/mk/spdk.modules.mk

APP := hello_zns_io

CFLAGS += -O3 -I $(ROOT_DIR)/include
C_SRCS := $(wildcard $(ROOT_DIR)/lib/*.c) $(APP).c
LIB += -L $(ROOT_DIR)/lib

SPDK_LIB_LIST = $(SOCK_MODULES_LIST) nvme vmd

include $(SPDK_ROOT_DIR)/mk/spdk.app.mk

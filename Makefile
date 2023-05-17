ROOT_DIR := $(realpath ./)
SCRIPTS_DIR := $(ROOT_DIR)/scripts
SPDK_DIR := $(ROOT_DIR)/spdk

.PHONY: all build_emulator build_zns_environment start_emulator check_zns unbind_zns rebind_zns clean

all:
	@echo "\nbuild_emulator			|	Building the specific version of QEMU"
	@echo "build_zns_environment		|	Building environment for zoned namespaces SSD in guest OS"
	@echo "start_emulator			|	Emulate a computer with zoned namespaces SSD"
	@echo "unbind_zns			|	Unbind ZNS from native kernel driver to SPDK"
	@echo "rebind_zns			|	Rebind ZNS from SPDK to native kernel driver"
	@echo "check_zns			|	Verify the SSD"
	@echo "clean				|	Clean"

build_emulator:
	@echo "\nEmulate configuration\n"
	@cat $(SCRIPTS_DIR)/host_emu/config
	@sudo sh $(SCRIPTS_DIR)/host_emu/build_emu.sh

build_zns_environment:
	@echo "\nZoned namespaces configuration\n"
	@cat $(SCRIPTS_DIR)/vm_zns/config
	@sudo sh $(SCRIPTS_DIR)/vm_zns/build_zns_env.sh

start_emulator:
	@echo "\nEmulate configuration\n"
	@cat $(SCRIPTS_DIR)/host_emu/config
	@sudo sh $(SCRIPTS_DIR)/host_emu/start_vm.sh

check_zns:
	@echo "\nZoned namespaces configuration\n"
	@cat $(SCRIPTS_DIR)/vm_zns/config
	@sudo sh $(SCRIPTS_DIR)/vm_zns/verify_zns_disk.sh

unbind_zns:
	@echo "\nUnbind ZNS from native kernel driver\n"
	@echo "Huge-page size: $(shell grep Hugepagesize /proc/meminfo | cut -d : -f 2 | tr -dc '0-9') KB"
	@echo "Total huge-pages memory: 2048 MB"
	@sudo $(SPDK_DIR)/scripts/setup.sh 

rebind_zns:
	@echo "\nRebind ZNS back to the kernel driver\n"
	@sudo $(SPDK_DIR)/scripts/setup.sh reset

clean:
	sudo rm -r $(ROOT_DIR)/output/

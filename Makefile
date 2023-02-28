ROOT_DIR := $(realpath ./)
SCRIPTS_DIR := $(ROOT_DIR)/scripts

.PHONY: all build_emulator build_zns_environment start_emulator check_zns clean

all:
	@echo "\nbuild_emulator | Building the specific version of QEMU\n"
	@echo "build_zns_environment | Building environment for zoned namespaces SSD in guest OS\n"
	@echo "start_emulator | Emulate a computer with zoned namespaces SSD\n"
	@echo "clean\n"

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

clean:
	sudo rm -r $(ROOT_DIR)/output/

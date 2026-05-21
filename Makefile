# freeNT OS - Makefile

.PHONY: all kernel shell clean distclean build-dir install help run-shell test iso

# Directories
BUILD_DIR := build
INSTALL_DIR := install
ISO_DIR := iso_build

# Default target
all: kernel shell

# Create build directory
build-dir:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(INSTALL_DIR)

# Build kernel
kernel: build-dir
	@echo "Building freeNT kernel..."
	@cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=Release .. && make freeNT

# Build shell
shell: build-dir
	@echo "Building Toriginal OS shell..."
	@cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=Release .. && make toriginal_shell

# Install
install: kernel shell
	@echo "Installing to $(INSTALL_DIR)..."
	@mkdir -p $(INSTALL_DIR)/bin
	@mkdir -p $(INSTALL_DIR)/boot
	@cp $(BUILD_DIR)/freeNT $(INSTALL_DIR)/boot/
	@cp $(BUILD_DIR)/toriginal_shell $(INSTALL_DIR)/bin/
	@echo "Installation complete!"

# Run shell (for testing)
run-shell: shell
	@echo "Starting Toriginal OS Shell..."
	@bash ./test-shell.sh

# Test shell directly
test: shell
	@SHELL_PATH=$$(find build -name "toriginal_shell" -type f 2>/dev/null | head -1); \
	if [ -z "$$SHELL_PATH" ]; then \
		echo "Error: Shell not found. Build may have failed."; \
		exit 1; \
	fi; \
	$$SHELL_PATH

# Create bootable ISO
iso: kernel
	@echo "Creating bootable ISO image..."
	@mkdir -p $(ISO_DIR)/boot/grub
	@cp $(BUILD_DIR)/freeNT $(ISO_DIR)/boot/
	@cp freeNT/src/kernel/boot/grub.cfg $(ISO_DIR)/boot/grub/
	@which grub-mkrescue > /dev/null 2>&1 && \
		grub-mkrescue -o freeNT.iso $(ISO_DIR)/ && \
		echo "ISO created: freeNT.iso" || \
		echo "Warning: grub-mkrescue not found. Install grub2-tools to create ISO."
	@echo "To test with QEMU: qemu-system-x86_64 -cdrom freeNT.iso"

# Clean
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)
	@rm -rf $(ISO_DIR)

# Distclean (complete cleanup)
distclean: clean
	@rm -rf $(INSTALL_DIR)
	@rm -f freeNT.iso
	@find . -name "*.o" -delete
	@find . -name "*.a" -delete
	@echo "Cleaned all build artifacts"

# Help
help:
	@echo "freeNT OS - Build Targets"
	@echo ""
	@echo "  make all         - Build kernel and shell (default)"
	@echo "  make kernel      - Build freeNT kernel only"
	@echo "  make shell       - Build Toriginal OS shell only"
	@echo "  make install     - Install to $(INSTALL_DIR)"
	@echo "  make test        - Run shell directly"
	@echo "  make run-shell   - Run shell with banner"
	@echo "  make iso         - Create bootable ISO image"
	@echo "  make clean       - Remove build directory"
	@echo "  make distclean   - Remove all build artifacts"
	@echo "  make help        - Show this help message"

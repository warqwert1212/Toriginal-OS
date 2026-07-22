CC      := x86_64-linux-gnu-gcc
LD      := x86_64-linux-gnu-ld
AS      := x86_64-linux-gnu-as
MKISO   := grub-mkrescue

SRC     := kernel
SYS     := ../sys/shell
BOOT    := kernel/boot
OBJ     := obj
BIN     := bin
ISO_DIR := isodir


GCC_FREESTANDING_INCLUDE := $(shell $(CC) -print-file-name=include)

CFLAGS  := -ffreestanding -nostdlib -nostdinc -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -O2 -Wall -Wextra -Iinclude -Ikernel -I$(GCC_FREESTANDING_INCLUDE)
LDFLAGS := -T kernel/boot/kernel64.ld --no-undefined -z noexecstack

C_SRCS  := $(wildcard $(SRC)/*.c)
S_SRCS  := $(wildcard $(BOOT)/*.s) $(wildcard $(BOOT)/*.S)
SYS_SRCS := $(wildcard $(SYS)/*.c)

C_OBJS  := $(patsubst $(SRC)/%.c,  $(OBJ)/%.o,      $(C_SRCS))
S_OBJS  := $(patsubst $(BOOT)/%.s, $(OBJ)/boot/%.o, $(S_SRCS))
SYS_OBJS := $(patsubst $(SYS)/%.c, $(OBJ)/sys/%.o,  $(SYS_SRCS))

OBJS := $(OBJ)/boot/boot64.o \
        $(filter-out $(OBJ)/boot/boot64.o, $(S_OBJS)) \
        $(C_OBJS) \
        $(SYS_OBJS)

.PHONY: all iso run clean tools

all: iso

$(OBJ)/boot $(BIN) $(ISO_DIR)/boot/grub:
	mkdir -p $@

$(OBJ)/%.o: $(SRC)/%.c | $(OBJ)/boot
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@


$(OBJ)/graphics_3d.o: $(SRC)/graphics_3d.c | $(OBJ)/boot
	mkdir -p $(dir $@)
	$(CC) $(filter-out -mno-sse -mno-sse2,$(CFLAGS)) -msse -msse2 -mfpmath=sse -c $< -o $@

$(OBJ)/sys/%.o: $(SYS)/%.c | $(OBJ)/boot
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ)/boot/%.o: $(BOOT)/%.s | $(OBJ)/boot
	$(AS) --64 $< -o $@

$(OBJ)/boot/%.o: $(BOOT)/%.S | $(OBJ)/boot
	$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@

$(BIN)/kernel.elf: $(OBJS) kernel/boot/kernel64.ld | $(BIN)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	@echo "── ELF OK ──"
	@x86_64-linux-gnu-readelf -h $@ | grep -E "Class|Machine|Entry"


ASSET_WALLPAPERS_SRC := ../sys/gui/assets/desktop/wallpapers
ASSET_STARTMENU_SRC  := ../sys/gui/assets/desktop/startmenu
ASSET_CURSOR_SRC     := ../sys/gui/assets/desktop/mice

ASSET_WALLPAPERS_NAMES := $(shell find $(ASSET_WALLPAPERS_SRC) -maxdepth 1 -name '*.png' ! -name '* *' -printf '%f\n' 2>/dev/null)
ASSET_STARTMENU_NAMES  := $(shell find $(ASSET_STARTMENU_SRC)  -maxdepth 1 -name '*.png' ! -name '* *' -printf '%f\n' 2>/dev/null)
ASSET_CURSOR_NAMES     := $(shell find $(ASSET_CURSOR_SRC)     -maxdepth 1 -name '*.png' ! -name '* *' -printf '%f\n' 2>/dev/null)


ASSET_WALLPAPERS_ISO := $(addprefix $(ISO_DIR)/boot/assets/wallpapers/,$(ASSET_WALLPAPERS_NAMES))
ASSET_STARTMENU_ISO  := $(addprefix $(ISO_DIR)/boot/assets/startmenu/,$(ASSET_STARTMENU_NAMES))
ASSET_CURSOR_ISO     := $(addprefix $(ISO_DIR)/boot/assets/cursor/,$(ASSET_CURSOR_NAMES))

$(ISO_DIR)/boot/assets/wallpapers/%.png: $(ASSET_WALLPAPERS_SRC)/%.png | $(ISO_DIR)/boot/grub
	mkdir -p $(dir $@)
	cp "$<" $@

$(ISO_DIR)/boot/assets/startmenu/%.png: $(ASSET_STARTMENU_SRC)/%.png | $(ISO_DIR)/boot/grub
	mkdir -p $(dir $@)
	cp "$<" $@

$(ISO_DIR)/boot/assets/cursor/%.png: $(ASSET_CURSOR_SRC)/%.png | $(ISO_DIR)/boot/grub
	mkdir -p $(dir $@)
	cp "$<" $@



$(ISO_DIR)/boot/grub/grub.cfg: | $(ISO_DIR)/boot/grub
	@{ \
	  echo 'set timeout=5'; \
	  echo 'set default=0'; \
	  echo ''; \
	  echo 'insmod vbe'; \
	  echo 'insmod video_bochs'; \
	  echo 'insmod video_cirrus'; \
	  echo 'insmod efi_gop'; \
	  echo 'insmod all_video'; \
	  echo 'set gfxpayload=keep'; \
	  echo ''; \
	  emit_entry() { \
	    name="$$1"; extra_args="$$2"; \
	    echo "menuentry \"$$name\" {"; \
	    if [ -n "$$extra_args" ]; then \
	      echo "  multiboot2 /boot/kernel.elf $$extra_args"; \
	    else \
	      echo "  multiboot2 /boot/kernel.elf"; \
	    fi; \
	    for f in $(ASSET_WALLPAPERS_NAMES); do \
	      echo "  module2 /boot/assets/wallpapers/$$f /system/gui/wallpapers/$$f"; \
	    done; \
	    for f in $(ASSET_STARTMENU_NAMES); do \
	      echo "  module2 /boot/assets/startmenu/$$f /system/gui/startmenu/$$f"; \
	    done; \
	    for f in $(ASSET_CURSOR_NAMES); do \
	      echo "  module2 /boot/assets/cursor/$$f /sys/gui/assets/$$f"; \
	    done; \
	    echo "  boot"; \
	    echo "}"; \
	    echo ""; \
	  }; \
	  emit_entry "toriginal_OS" ""; \
	  emit_entry "toriginal_OS (start in GUI)" "gui"; \
	} > $@

$(ISO_DIR)/boot/kernel.elf: $(BIN)/kernel.elf | $(ISO_DIR)/boot/grub
	cp $< $@

toriginal_OS.iso: $(ISO_DIR)/boot/kernel.elf $(ISO_DIR)/boot/grub/grub.cfg \
                   $(ASSET_WALLPAPERS_ISO) $(ASSET_STARTMENU_ISO) $(ASSET_CURSOR_ISO)
	$(MKISO) -o $@ $(ISO_DIR) -d /usr/lib/grub/i386-pc

iso: toriginal_OS.iso

run: iso
	qemu-system-x86_64 -cdrom toriginal_OS.iso -m 256M -serial stdio -no-reboot


run-headless: iso
	qemu-system-x86_64 -cdrom toriginal_OS.iso -m 256M -serial stdio -display none -no-reboot

tools:
	sudo apt update && sudo apt install -y \
	    gcc-x86-64-linux-gnu binutils-x86-64-linux-gnu \
	    nasm xorriso grub-pc-bin mtools qemu-system-x86

clean:
	rm -rf $(OBJ) $(BIN) $(ISO_DIR) toriginal_OS.iso

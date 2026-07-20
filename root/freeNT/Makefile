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

$(ISO_DIR)/boot/grub/grub.cfg: | $(ISO_DIR)/boot/grub
	@printf 'set timeout=5\nset default=0\n\ninsmod vbe\ninsmod video_bochs\ninsmod video_cirrus\ninsmod efi_gop\ninsmod all_video\nset gfxpayload=keep\n\nmenuentry "toriginal_OS" {\n\tmultiboot2 /boot/kernel.elf\n\tmodule2 /boot/assets/wallpapers/sky_blue.png /system/gui/wallpapers/sky_blue.png\n\tmodule2 /boot/assets/wallpapers/pinetree.png /system/gui/wallpapers/pinetree.png\n\tmodule2 /boot/assets/startmenu/orb.png /system/gui/startmenu/orb.png\n\tmodule2 /boot/assets/startmenu/orb_hover.png /system/gui/startmenu/orb_hover.png\n\tmodule2 /boot/assets/startmenu/taskbar.png /system/gui/startmenu/taskbar.png\n\tmodule2 /boot/assets/startmenu/panel.png /system/gui/startmenu/panel.png\n\tmodule2 /boot/assets/startmenu/programs.png /system/gui/startmenu/programs.png\n\tmodule2 /boot/assets/startmenu/power.png /system/gui/startmenu/power.png\n\tmodule2 /boot/assets/cursor/cursor_arrow.png /sys/gui/assets/cursor_arrow.png\n\tmodule2 /boot/assets/cursor/cursor_hand.png /sys/gui/assets/cursor_hand.png\n\tboot\n}\n\nmenuentry "toriginal_OS (start in GUI)" {\n\tmultiboot2 /boot/kernel.elf gui\n\tmodule2 /boot/assets/wallpapers/sky_blue.png /system/gui/wallpapers/sky_blue.png\n\tmodule2 /boot/assets/wallpapers/pinetree.png /system/gui/wallpapers/pinetree.png\n\tmodule2 /boot/assets/startmenu/orb.png /system/gui/startmenu/orb.png\n\tmodule2 /boot/assets/startmenu/orb_hover.png /system/gui/startmenu/orb_hover.png\n\tmodule2 /boot/assets/startmenu/taskbar.png /system/gui/startmenu/taskbar.png\n\tmodule2 /boot/assets/startmenu/panel.png /system/gui/startmenu/panel.png\n\tmodule2 /boot/assets/startmenu/programs.png /system/gui/startmenu/programs.png\n\tmodule2 /boot/assets/startmenu/power.png /system/gui/startmenu/power.png\n\tmodule2 /boot/assets/cursor/cursor_arrow.png /sys/gui/assets/cursor_arrow.png\n\tmodule2 /boot/assets/cursor/cursor_hand.png /sys/gui/assets/cursor_hand.png\n\tboot\n}\n' > $@

# Wallpaper PNGs shipped as real multiboot modules (see kernel.c's
# seed_boot_modules_to_fs()) - not embedded in the ELF. Source names
# have spaces/special chars that are awkward as GRUB module cmdlines,
# so they're copied under sanitized names here; the sanitized name is
# cosmetic only, the destination TRPFS path in grub.cfg above is what
# actually matters to wallpaper.c.
ASSET_WALLPAPERS_SRC := ../sys/gui/assets/desktop/wallpapers
ASSET_STARTMENU_SRC  := ../sys/gui/assets/desktop/startmenu
ASSET_CURSOR_SRC     := ../sys/gui/assets/desktop/mice

$(ISO_DIR)/boot/assets/wallpapers/sky_blue.png: | $(ISO_DIR)/boot/grub
	mkdir -p $(dir $@)
	cp "$(ASSET_WALLPAPERS_SRC)/sky blu.png" $@

$(ISO_DIR)/boot/assets/wallpapers/pinetree.png: | $(ISO_DIR)/boot/grub
	mkdir -p $(dir $@)
	cp "$(ASSET_WALLPAPERS_SRC)/unnamedpinetree^_^.png" $@

# Start menu assets - already sanely named at their repo source, so
# these are a straight copy, no renaming needed like the wallpapers
# above.
$(ISO_DIR)/boot/assets/startmenu/%.png: | $(ISO_DIR)/boot/grub
	mkdir -p $(dir $@)
	cp "$(ASSET_STARTMENU_SRC)/$*.png" $@

# Cursor images already exist in the repo (root/sys/gui/assets/) but,
# same gap as the wallpapers before this change, were never actually
# wired onto the boot media - cursor.c has been reading real file
# paths that nothing ever populated. "cursor_arrow" is the normal
# pointer, "cursor_hand" is the click/interactive state - matching
# cursor.h's CURSOR_ASSET_PATH_ARROW / CURSOR_ASSET_PATH_HAND.
$(ISO_DIR)/boot/assets/cursor/cursor_arrow.png: | $(ISO_DIR)/boot/grub
	mkdir -p $(dir $@)
	cp "$(ASSET_CURSOR_SRC)/toriginal cursor.png" $@

$(ISO_DIR)/boot/assets/cursor/cursor_hand.png: | $(ISO_DIR)/boot/grub
	mkdir -p $(dir $@)
	cp "$(ASSET_CURSOR_SRC)/toriginal cursor click.png" $@

$(ISO_DIR)/boot/kernel.elf: $(BIN)/kernel.elf | $(ISO_DIR)/boot/grub
	cp $< $@

STARTMENU_ASSETS := orb orb_hover taskbar panel programs power

toriginal_OS.iso: $(ISO_DIR)/boot/kernel.elf $(ISO_DIR)/boot/grub/grub.cfg \
                   $(ISO_DIR)/boot/assets/wallpapers/sky_blue.png \
                   $(ISO_DIR)/boot/assets/wallpapers/pinetree.png \
                   $(ISO_DIR)/boot/assets/cursor/cursor_arrow.png \
                   $(ISO_DIR)/boot/assets/cursor/cursor_hand.png \
                   $(foreach a,$(STARTMENU_ASSETS),$(ISO_DIR)/boot/assets/startmenu/$(a).png)
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

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
# bootloader_blobs.s is deliberately excluded from S_SRCS/S_OBJS/OBJS -
# it needs its own rule (real cpp preprocessing + absolute -D paths,
# see its dedicated rule below) and is only linked into the PASS 2
# build, never pass 1 - see the TWO-PASS BUILD note below for why.
S_SRCS  := $(filter-out $(BOOT)/bootloader_blobs.s $(BOOT)/asset_blob.s $(BOOT)/trbl_logo_blob.s, $(wildcard $(BOOT)/*.s)) $(wildcard $(BOOT)/*.S)
SYS_SRCS := $(wildcard $(SYS)/*.c)

# installer.c references the bootloader blob symbols
# (g_kernel_elf_blob etc.) that only exist once bootloader_blobs.o is
# linked in - which PASS 1 deliberately never does (see TWO-PASS BUILD
# below). So installer.c needs two distinct object files: one compiled
# with -DPASS1_BUILD (uses the no-op stub, see installer.c's #ifndef
# PASS1_BUILD guard around write_bootloader_to_disk()) for PASS 1's
# link, and one without it for PASS 2's real, shipped link. Every
# other .c file is identical between passes and only compiled once.
C_SRCS_NO_INSTALLER := $(filter-out $(SRC)/installer.c, $(C_SRCS))
C_OBJS_SHARED := $(patsubst $(SRC)/%.c, $(OBJ)/%.o, $(C_SRCS_NO_INSTALLER))
S_OBJS   := $(patsubst $(BOOT)/%.s, $(OBJ)/boot/%.o, $(S_SRCS))
SYS_OBJS := $(patsubst $(SYS)/%.c, $(OBJ)/sys/%.o,  $(SYS_SRCS))

OBJS_COMMON := $(OBJ)/boot/boot64.o \
        $(filter-out $(OBJ)/boot/boot64.o, $(S_OBJS)) \
        $(OBJ)/boot/asset_blob.o \
        $(OBJ)/boot/trbl_logo_blob.o \
        $(C_OBJS_SHARED) \
        $(SYS_OBJS)

# PASS 1: every object except bootloader_blobs.o, installer.c compiled
# with PASS1_BUILD defined (stub write_bootloader_to_disk()). This is
# what actually gets embedded into PASS 2 and written to disk.
OBJS_PASS1 := $(OBJS_COMMON) $(OBJ)/installer_pass1.o

# PASS 2: same as PASS 1 but with the real installer.o (full
# write_bootloader_to_disk()) plus bootloader_blobs.o. This is what
# actually ships as bin/kernel.elf / the ISO.
OBJS := $(OBJS_COMMON) $(OBJ)/installer.o

.PHONY: all iso run clean tools esp run-uefi

all: iso

$(OBJ)/boot $(BIN) $(ISO_DIR)/boot/grub:
	mkdir -p $@

$(OBJ)/%.o: $(SRC)/%.c | $(OBJ)/boot
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# PASS 1 variant of installer.o - see the OBJS_PASS1/OBJS split above
# for why this needs to be a separate object from the normal one.
$(OBJ)/installer_pass1.o: $(SRC)/installer.c | $(OBJ)/boot
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DPASS1_BUILD -c $< -o $@


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

# ── PASS 1: the plain kernel, no embedded self-copy ─────────────────────
# This is the exact binary that gets embedded into PASS 2 below and
# written to disk by installer.c's write_bootloader_to_disk() - it must
# be a completely ordinary build (same flags, same sources minus the
# blob file) so it's byte-identical to what a from-scratch build always
# produces, and so PASS 2 only differs by the presence of the blob.
$(BIN)/kernel_pass1.elf: $(OBJS_PASS1) kernel/boot/kernel64.ld | $(BIN)
	$(LD) $(LDFLAGS) -o $@ $(OBJS_PASS1)
	@echo "── PASS 1 (no bootloader blob) ELF OK ──"
	@x86_64-linux-gnu-readelf -h $@ | grep -E "Class|Machine|Entry"

# BUILD ORDER NOTE: before PASS 1 even links, kernel/boot/loader/
# stage1.bin and stage2.bin must exist (bootloader_blobs.s, built in
# PASS 2 below, .incbin's them) - build them unconditionally up front,
# same as before. Skipped for `clean`/`tools` since neither needs them.
ifeq ($(filter clean tools,$(MAKECMDGOALS)),)
$(shell kernel/boot/loader/build_loader.sh >&2)
endif

# ── PASS 2: embeds PASS 1's exact kernel.elf, relinks ───────────────────
# bootloader_blobs.o .incbin's $(BIN)/kernel_pass1.elf (not
# kernel/boot/loader/*.bin's TWO-PASS-BUILD-unrelated stage1/stage2,
# which it also embeds - see that file) alongside stage1.bin/stage2.bin,
# then this final link swaps it in for a completely ordinary relink of
# the exact same $(OBJS) - nothing else about the build differs between
# passes, so PASS 2's kernel.elf is byte-identical to PASS 1's except
# for the added .rodata blob data, and the disk-embedded copy
# installer.c writes (PASS 1's output) is exactly what actually runs on
# the metal once booted from disk, checksum-stable and reproducible.
$(OBJ)/boot/bootloader_blobs.o: $(BOOT)/bootloader_blobs.s kernel/boot/loader/stage1.bin kernel/boot/loader/stage2.bin $(BIN)/kernel_pass1.elf | $(OBJ)/boot
	$(CC) $(CFLAGS) -x assembler-with-cpp \
	    -DSTAGE1_BIN_PATH='"$(CURDIR)/kernel/boot/loader/stage1.bin"' \
	    -DSTAGE2_BIN_PATH='"$(CURDIR)/kernel/boot/loader/stage2.bin"' \
	    -DKERNEL_ELF_PATH='"$(CURDIR)/$(BIN)/kernel_pass1.elf"' \
	    -c $< -o $@

$(BIN)/kernel.elf: $(OBJS) $(OBJ)/boot/bootloader_blobs.o kernel/boot/kernel64.ld | $(BIN)
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(OBJ)/boot/bootloader_blobs.o
	@echo "── PASS 2 (with embedded bootloader + kernel copy) ELF OK ──"
	@x86_64-linux-gnu-readelf -h $@ | grep -E "Class|Machine|Entry"

# ── Embedded assets + boot logo ──────────────────────────────────────────
# asset_blob.bin is regenerated whenever any wallpaper/start-menu-
# image/cursor file changes or a new one is added - ASSET_*_NAMES
# (defined further down) is the same dynamic `find`-based discovery
# the ISO build already uses, so this and the ISO can never drift out
# of sync with each other or with what's actually in those folders.
$(BIN)/asset_blob.bin: kernel/boot/build_asset_blob.py \
        $(addprefix $(ASSET_WALLPAPERS_SRC)/,$(ASSET_WALLPAPERS_NAMES)) \
        $(addprefix $(ASSET_STARTMENU_SRC)/,$(ASSET_STARTMENU_NAMES)) \
        $(addprefix $(ASSET_CURSOR_SRC)/,$(ASSET_CURSOR_NAMES)) \
        | $(BIN)
	python3 kernel/boot/build_asset_blob.py $@

$(OBJ)/boot/asset_blob.o: $(BOOT)/asset_blob.s $(BIN)/asset_blob.bin | $(OBJ)/boot
	$(CC) $(CFLAGS) -x assembler-with-cpp \
	    -DASSET_BLOB_PATH='"$(CURDIR)/$(BIN)/asset_blob.bin"' \
	    -c $< -o $@

$(OBJ)/boot/trbl_logo_blob.o: $(BOOT)/trbl_logo_blob.s $(BOOT)/assets/trbl_logo.png | $(OBJ)/boot
	$(CC) $(CFLAGS) -x assembler-with-cpp \
	    -DTRBL_LOGO_PATH='"$(CURDIR)/$(BOOT)/assets/trbl_logo.png"' \
	    -c $< -o $@


# ── TRBLU (UEFI half of TRBL) ────────────────────────────────────────────
# main.c/efi.h build against MS x64 ABI (-mabi=ms, -fshort-wchar) since
# that's what every UEFI firmware call requires - a different, one-off
# set of flags from CFLAGS above, which is why this isn't just another
# pattern rule alongside the kernel's normal $(OBJ)/%.o one.
UEFI_DIR    := kernel/boot/uefi
UEFI_CFLAGS := -ffreestanding -fno-stack-protector -mno-red-zone -fpic -fshort-wchar -mabi=ms -O2 -Wall -Wextra -Iinclude

$(OBJ)/boot/uefi_main.o: $(UEFI_DIR)/main.c $(UEFI_DIR)/efi.h include/trbl_settings.h | $(OBJ)/boot
	$(CC) $(UEFI_CFLAGS) -c $< -o $@

$(BIN)/BOOTX64.EFI: $(OBJ)/boot/uefi_main.o | $(BIN)
	$(LD) -m i386pep $< -o $@ --subsystem 10 -e efi_main --image-base 0x0
	@echo "── TRBLU BOOTX64.EFI OK ──"
	@file $@ 2>/dev/null || true

# 1.44 MiB FAT12 image holding \EFI\BOOT\BOOTX64.EFI, the exact path
# UEFI firmware looks for by default. Uses mtools (already in the
# `tools` target below) rather than mounting a loop device, so this
# works without root and without a real filesystem driver on the
# build machine.
$(BIN)/esp.img: $(BIN)/BOOTX64.EFI | $(BIN)
	rm -f $@
	dd if=/dev/zero of=$@ bs=512 count=2880 status=none
	mformat -i $@ -f 1440 ::
	mmd -i $@ ::/EFI
	mmd -i $@ ::/EFI/BOOT
	mcopy -i $@ $< ::/EFI/BOOT/BOOTX64.EFI
	@echo "── ESP image OK: $@ ──"

esp: $(BIN)/esp.img

run-uefi: esp
	qemu-system-x86_64 -bios /usr/share/OVMF/OVMF_CODE.fd -drive file=$(BIN)/esp.img,format=raw -m 512M -serial stdio -no-reboot


ASSET_WALLPAPERS_SRC := ../sys/gui/assets/desktop/wallpapers
ASSET_STARTMENU_SRC  := ../sys/gui/assets/desktop/startmenu
ASSET_CURSOR_SRC     := ../sys/gui/assets/desktop/mice

ASSET_WALLPAPERS_NAMES := $(shell find $(ASSET_WALLPAPERS_SRC) -maxdepth 1 -type f ! -name '* *' -printf '%f\n' 2>/dev/null)
ASSET_STARTMENU_NAMES  := $(shell find $(ASSET_STARTMENU_SRC)  -maxdepth 1 -type f ! -name '* *' -printf '%f\n' 2>/dev/null)
ASSET_CURSOR_NAMES     := $(shell find $(ASSET_CURSOR_SRC)     -maxdepth 1 -type f ! -name '* *' -printf '%f\n' 2>/dev/null)


ASSET_WALLPAPERS_ISO := $(addprefix $(ISO_DIR)/boot/assets/wallpapers/,$(ASSET_WALLPAPERS_NAMES))
ASSET_STARTMENU_ISO  := $(addprefix $(ISO_DIR)/boot/assets/startmenu/,$(ASSET_STARTMENU_NAMES))
ASSET_CURSOR_ISO     := $(addprefix $(ISO_DIR)/boot/assets/cursor/,$(ASSET_CURSOR_NAMES))

$(ISO_DIR)/boot/assets/wallpapers/%: $(ASSET_WALLPAPERS_SRC)/% | $(ISO_DIR)/boot/grub
	mkdir -p $(dir $@)
	cp "$<" $@

$(ISO_DIR)/boot/assets/startmenu/%: $(ASSET_STARTMENU_SRC)/% | $(ISO_DIR)/boot/grub
	mkdir -p $(dir $@)
	cp "$<" $@

$(ISO_DIR)/boot/assets/cursor/%: $(ASSET_CURSOR_SRC)/% | $(ISO_DIR)/boot/grub
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

# ── Hybrid ISO: one disc, both boot paths ────────────────────────────────
# Built in a single grub-mkrescue/xorriso session (not a separate
# "modify an existing ISO" pass) - GRUB's BIOS El Torito setup and
# TRBLU's appended UEFI ESP partition both get added to the same
# xorriso session by forwarding extra options after `--`, which
# grub-mkrescue passes straight through to its underlying xorriso
# call. This replaces an earlier two-pass -indev/-outdev/-commit
# design that looked reasonable but silently did nothing (xorriso
# treated the two files as separate sessions and never actually
# copied content between them - "No image modifications pending").
# One session avoids that whole class of problem: BIOS boot code is
# never touched, TRBLU's partition is just added alongside it before
# anything is ever written to disk.
#
# HONESTY NOTE: still unverified - neither grub-mkrescue nor xorriso
# exist in the sandbox this was written in, so this is built from
# grub-mkrescue's documented `--` passthrough behavior and xorriso's
# own -boot_image/-append_partition option list (confirmed real via
# `xorriso -help` on the actual target machine), not confirmed
# end-to-end here.
toriginal_OS.iso: $(ISO_DIR)/boot/kernel.elf $(ISO_DIR)/boot/grub/grub.cfg \
                   $(ASSET_WALLPAPERS_ISO) $(ASSET_STARTMENU_ISO) $(ASSET_CURSOR_ISO) \
                   $(BIN)/esp.img
	$(MKISO) -o $@ $(ISO_DIR) -d /usr/lib/grub/i386-pc -- \
	    -boot_image any partition_table=on \
	    -boot_image any appended_part_as=gpt \
	    -append_partition 2 0xef $(BIN)/esp.img
	@echo "── Hybrid ISO OK: $@ (BIOS via GRUB + UEFI via TRBLU, one xorriso session) ──"

iso: toriginal_OS.iso

run: iso
	qemu-system-x86_64 -cdrom toriginal_OS.iso -m 256M -serial stdio -no-reboot


run-headless: iso
	qemu-system-x86_64 -cdrom toriginal_OS.iso -m 256M -serial stdio -display none -no-reboot

tools:
	sudo apt update && sudo apt install -y \
	    gcc-x86-64-linux-gnu binutils-x86-64-linux-gnu \
	    nasm xorriso grub-pc-bin mtools qemu-system-x86 ovmf

clean:
	rm -rf $(OBJ) $(BIN) $(ISO_DIR) toriginal_OS.iso

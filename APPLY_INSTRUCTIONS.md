# freeNT fix — apply instructions

Verified: this tree compiles and links into a valid bootable multiboot2
ELF64 with `x86_64-linux-gnu-gcc`/`as`/`ld`, **zero warnings, zero errors**.
I built it for real in my sandbox, not just read the code.

## 1. Delete these (dead/duplicate/broken)

Run from your repo root (`/workspaces/Toriganal-OS/`):

```bash
rm -rf root/file_formats
rm -rf root/installer
rm -f  root/freeNT/kernel/Makefile
rm -f  root/freeNT/kernel/CMakeLists.txt
rm -f  root/freeNT/kernel/interrups.c
rm -f  root/freeNT/kernel/keybord.c
rm -f  root/freeNT/kernel/isr.c
rm -f  root/freeNT/kernel/loader_enhaced.c
rm -f  root/freeNT/kernel/trp_manifest.c.bak
rm -f  root/freeNT/include/keybord.h
rm -f  root/freeNT/include/vmm.h
rm -f  root/freeNT/kernel/vmm.c
rm -f  root/sys/gui/CMakeLists.txt
```

**Why each one:**
- `root/file_formats/` — exact duplicate of `ntfs.c/h`, `trpfs.c/h`; canonical copies now live under `root/freeNT/`.
- `root/installer/` — moved into `root/freeNT/kernel/` so the Makefile actually compiles it (it wasn't being built at all before).
- `kernel/Makefile` — broken, wrong relative paths, never worked.
- `kernel/CMakeLists.txt` — referenced a `src/kernel/...` tree that doesn't exist anywhere in the repo.
- `interrups.c`, `keybord.c`, `loader_enhaced.c` — typo'd filenames, replaced by correctly-named versions below.
- `isr.c` — dead unused global, confusing near-duplicate of `interrupts.c`'s real dispatch table.
- `trp_manifest.c.bak` — triple-duplicated dead content.
- `keybord.h` — typo, replaced by `keyboard.h`.
- `vmm.h` / `vmm.c` — a second, incompatible paging implementation (assumes a recursive PML4 slot that boot64.s never sets up — would triple-fault if ever called). `memory.c`'s `mm_*` family is the one real implementation.
- `root/sys/gui/CMakeLists.txt` — part of the abandoned CMake build.

**Leave untouched** (not part of the kernel build, safe to keep as-is):
- `root/sys/gui/*.c` (desktop.c, taskbar.c, startbuttton.c, time_finder.c) — needs a framebuffer/VBE driver and PS/2 mouse driver that don't exist yet. See "What's still missing" below.
- `root/sys/shell/include/shell.h`, `root/sys/shell/src/main.cpp`, `root/sys/shell/src/shell.cpp.bak` — a C++ prototype for a future *userspace* shell (assumes a hosted libc — `iostream`, `std::string`, `system()` — none of which exist in a freestanding kernel). Not buildable today, not meant to be.
- `root/sys/userpc/home/useraccount1/apps/oobe.c` — a richer OOBE design that depends on GUI/auth hooks that don't exist yet. Kept as a reference for when the GUI layer is built.

## 2. Extract the fixed files

Download `freeNT-fixed-tree.tar.gz` and extract it **over your repo root** (it preserves the same `root/...` paths):

```bash
tar -xzf freeNT-fixed-tree.tar.gz -C /workspaces/Toriganal-OS/
```

This adds/overwrites:
- `root/freeNT/Makefile` (fixed)
- `root/freeNT/include/*.h` (all 28 headers — several fixed, `keyboard.h`/`mm.h`/`ntfs.h`/`trpfs.h`/`installer.h` notably)
- `root/freeNT/kernel/boot/*.s`, `kernel64.ld` (boot64.s and the linker script both carry a real fix — see below)
- `root/freeNT/kernel/*.c` (24 source files — includes the renamed/moved ones: `interrupts.c`, `keyboard.c`, `loader_enhanced.c`, `shell.c`, `installer.c`)
- `MANIFEST.txt` — my running log of every delete/keep/fix decision, for your reference

## 3. Build it

```bash
cd /workspaces/Toriganal-OS/root/freeNT
make tools      # one-time: installs grub-mkrescue, qemu, nasm, xorriso, etc.
make clean
make iso        # builds freeNT.iso
make run        # boots it in QEMU with serial output in your terminal
```

For VirtualBox specifically: take the generated `freeNT.iso` and attach it as the boot CD/DVD of a new VirtualBox VM (Type: Other, Version: Other/Unknown 64-bit, or "Other Linux 64-bit" — multiboot2 doesn't care, just needs a CD boot device and EFI disabled/BIOS boot mode).

## 4. What was actually broken (the important ones)

1. **The kernel would never have booted.** `boot64.s` declared `.section .multiboot` with no section flags, so the assembler never marked it `ALLOC`. A non-`ALLOC` section gets excluded from every loadable segment's address range, so the linker shoved it ~48 KB into the file — past the 8 KB window GRUB scans for the multiboot2 header. Result: "no bootable medium," every time, regardless of how correct the rest of the kernel was. Fixed by adding explicit flags: `.section .multiboot, "ax"`. I verified the exact byte offset of the multiboot magic in the final ELF — it's now at file offset `0x1000`, well inside the scan window.
2. **`install OS` was non-functional.** The shell's inline installer called `fs_mkdir`/`fs_open` without ever provisioning, formatting, or mounting a TRPFS volume first. Every call silently failed. The real installer (`installer.c`) existed and was correct but wasn't even being compiled — it lived in a directory (`root/installer/`) the Makefile never looked at. Moved it into the kernel build and rewired `shell.c` to call it for real.
3. **`process.c` and `shell.c` wouldn't compile** — both called `serial_puts`/`serial_putc` with no declaration in scope (implicit-declaration error under any modern C standard). Added the missing includes.
4. **Duplicate `kernel_os_shell()`** — a halt-forever stub in `kernel.c` and the real shell loop in `shell.c`, which wasn't in the build at all, so the stub was the only one that ever linked. Removed the stub, moved the real shell into the build.
5. Two parallel build systems (flat Makefile vs. CMake) where the CMake one pointed at a `src/kernel/...` source tree that doesn't exist anywhere in the repo. Standardized on the Makefile.
6. Filename typos breaking includes: `interrups.c`, `keybord.c/h`, `loader_enhaced.c`.
7. Hardcoded GCC version path (`/13/`) in the Makefile that would break the moment Codespaces ships a different default GCC. Made it self-detecting via `$(CC) -print-file-name=include`.

Full line-by-line reasoning for every change is in `MANIFEST.txt`.

## 5. What's still missing (not fixed, by design — these need actual new code, not cleanup)

- **GUI layer** (`root/sys/gui/*.c`): needs a pixel framebuffer/VBE driver (current `vga.c` is text-mode only, 80x25 character cells) and a PS/2 mouse driver (only keyboard exists). Also needs `sys_file_exists`/`sys_read_file`/`sys_execute_program`/`sys_draw_char`/`sys_draw_png`/RTC read-write shims and a `current_user` config struct — none of which exist yet.
- **ELF loader** (`loader_load_elf` in `loader_enhanced.c`): still a stub, returns -1 always.
- **Usermode/ring3**: `process_start_inplace` jumps directly in ring 0 — no privilege transition, no TSS, no ring3 GDT segments. `syscall_init()` sets up the MSRs but nothing uses them yet.
- **`timer.c`**: declares `timer_init`/`timer_ticks` in the header but never implements them (unused, harmless, just incomplete — `pit.c` is the timer that's actually wired in).

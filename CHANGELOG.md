# Toriginal OS — Changelog

## v1.3.0 (2026-08-23) — TRBLU: UEFI boot support

### Added
- **`kernel/boot/uefi/`** — TRBLU, the UEFI half of TRBL. A real,
  from-scratch PE32+ EFI application (`main.c` + `efi.h`), built with
  the existing `x86_64-linux-gnu-gcc`/`ld` toolchain (no gnu-efi, no
  mingw — verified this links a valid `EFI application x86-64` PE32+
  binary directly via `ld -m i386pep --subsystem 10`).
  - `efi.h`: minimal hand-written UEFI protocol/type definitions
    (Simple Text Output, Graphics Output, Block I/O, Loaded Image,
    Boot Services, System Table) — only the subset TRBLU actually
    calls, every function pointer correctly marked `ms_abi` (UEFI's
    calling convention on x86_64 differs from the rest of this
    codebase's System V).
  - `main.c`: locates the raw boot disk's Block I/O protocol, reads
    the TRBL settings sector (same LBA 2000 the BIOS path reads —
    see `trbl_settings.h`), sets the closest matching GOP mode, loads
    `kernel.elf`'s PT_LOAD segments from the same LBA 65 spare copy
    the BIOS path uses, locates `_start64_uefi` by walking the
    kernel's own ELF symbol table (not a hardcoded offset), builds
    the identical multiboot2-format info block
    `build_multiboot2_info` (stage2_pm.s) produces for BIOS, then
    `ExitBootServices` + jumps straight into the kernel — meaning
    `kernel_main()` needed zero changes to accept a UEFI boot.
  - `make_esp.py`: from-scratch FAT12 image builder (no mtools/
    mkdosfs available in this environment) producing an EFI System
    Partition image holding `\EFI\BOOT\BOOTX64.EFI`. Verified by
    reconstructing the file from the generated image's cluster chain
    and confirming a byte-exact match against the original.
  - `build_uefi.sh`: compiles and links `BOOTX64.EFI` end to end.
- **`kernel/boot/boot64.s`**: new `_start64_uefi` entry point. UEFI
  hands off already in 64-bit long mode with paging enabled (unlike
  the BIOS path, which has to climb out of real/protected mode
  itself) — this entry skips the CPUID/EFER/CR0 dance `_start` does,
  but still runs the exact same `clear_page_tables`/
  `build_page_tables` and switches `CR3` before falling into
  `long_mode_start`, so kernel memory layout is identical regardless
  of which boot path got it there.

### Known limitations (honest, not silently glossed over)
- `find_boot_disk()` requires the whole-disk Block I/O handle to be
  reachable directly from the boot device handle. If firmware only
  exposes the ESP as a logical partition with no direct path to the
  parent disk, TRBLU reports this plainly and halts rather than
  guessing at the wrong device — walking the GPT to find the parent
  disk from a partition handle is real follow-up work, not done here.
- Not boot-tested on real hardware or in an emulator — this
  environment has no QEMU/KVM and no real machine. Verified as much
  as static analysis allows (clean compile with zero warnings under
  the UEFI-appropriate flags, a real recognized PE32+ EFI binary per
  `file`, byte-exact FAT12 round-trip), but "boots on real UEFI
  firmware" is unverified until it's actually tried.

## v1.2.0 (2026-08-22) — TRBL: real dynamic bootloader settings

### Added
- **TRBL branding.** The from-scratch bootloader (stage1 MBR + stage2
  real-mode loader, `kernel/boot/loader/`) is now named TRBL —
  Toriginal OS Runtime Boot Loader. Boot-time messages updated from
  "TRP stage1/stage2" to "TRBL stage1/stage2" accordingly.
- **`include/trbl_settings.h` / `kernel/trbl_settings.c`.** A real,
  versioned, checksummed 512-byte on-disk settings sector (LBA 2000,
  mirrored to a backup at LBA 2001) holding boot-time display
  resolution, network mode (off/DHCP/static + static IP/mask/gateway/
  DNS), and a boot-flags bitmask. Read/write API with automatic
  fallback to the backup sector (and to compiled-in defaults) if the
  primary sector fails its checksum — protects against a torn write
  from a power loss mid-save.
- **`settings network <off|dhcp|static ip mask gw dns>`** shell
  command (`sys/shell/shell.c`), alongside `settings resolution` now
  also persisting to the TRBL settings sector (previously it only
  wrote `/toriginal_os/config.ini`, which the bootloader can't read —
  a resolution change took effect live but was lost on reboot).

### Changed
- **`kernel/boot/loader/stage2.s`** now reads the TRBL settings sector
  at every boot (`load_trbl_settings`) and applies its width/height to
  the VBE mode-set step, instead of `installer.c` byte-patching
  `saved_width`/`saved_height` directly into the compiled stage2
  binary at install time. Falls back silently to the compiled-in
  1024x768 default if the sector is missing/corrupt (fresh disk, or a
  disk error) — never fatal.
- **`installer.c`'s `write_bootloader_to_disk()`** now writes
  stage1.bin/stage2.bin byte-identical on every install (no more
  per-install binary patch) and instead calls the same
  `trbl_settings_write()` the shell uses, seeded from the current
  syscfg resolution. `kernel/boot/loader/stage2_offsets.h` and its
  `build_loader.sh` generation step are removed — no longer needed
  now that settings don't live inside the binary.
- **`kernel/kernel.c`** reads the TRBL settings sector once during
  boot (step `[8/8]`, after disk/filesystem bring-up) into the new
  `g_trbl_settings` global and logs the saved network intent. Actually
  acting on `TRBL_NET_DHCP`/`TRBL_NET_STATIC` automatically at boot
  (a real DHCP client call, or driving the static-assign path without
  a human at `ifconfig`) is follow-up work — this stage makes the
  saved intent visible, it does not yet act on it unattended.

## v1.1.0 (2026-08-01) — TRP execution fix + manifest system additions

### Fixed
- **`trp_load()` execution bug (the big one).** The loader parsed a TRP
  package's `manifest.txt` **twice**, with two completely different,
  incompatible grammars:
  1. The real one — `Execute at/:`, `Window name/:`, `Language/:`, etc. —
     implemented by `trp_manifest_parse()` and used for the initial gate.
     This is the grammar `trpbuild` (and now `trpc`) actually write.
  2. A second, dead-on-arrival grammar (`trp_pkg_manifest_t` /
     `pkg_parse()`) expecting lines like `/this is executable/` — a
     leading-slash format that no manifest anyone actually generates has
     ever used.

  A package would pass gate #1 (real grammar, valid manifest) and then
  immediately fail gate #2 (`is_executable` never got set, because the
  line it was looking for never existed), and `trp_load()` would refuse
  to run it every single time. **Every locally-built `.trp` package was
  silently unrunnable** via `run` or `trpm install`. Fixed by parsing the
  manifest exactly once, with the one real grammar, and reading
  executability/language/checksum off that single parse result.

### Added
- **`Checksum/:` manifest directive.** A 64-hex-char sha256 digest of the
  embedded payload. If present, `trp_load()` verifies it against the
  actual payload bytes and refuses to run on a mismatch. `trpbuild` and
  `trpc` both auto-compute and append this at build time if you don't
  supply one, so local builds are integrity-protected by default without
  any extra author effort.
- **`Description/:` and `Args/:` manifest directives** — human-readable
  summary and default launch args, surfaced by `trpinfo`/`trpm verify`.
- **`trpinfo <path.trp>`** shell command — dump a package's header and
  full manifest (including checksum match/mismatch) without running it.
- **`trpm verify <name>`** shell command — re-check an *installed*
  package's manifest and checksum against what's actually sitting in
  `/pkgs/` right now, catching on-disk corruption or tampering that
  happened after install (the HMAC check in remote installs only covers
  the download itself, not what happens to the file afterward).

### API changes (internal, backward compatible)
- `trp_manifest_validate_ex(m, skip_file_check)` / `trp_manifest_run_gate2(...)`
  added; old `trp_manifest_validate()` / `trp_manifest_run_gate()` kept as
  thin back-compat wrappers, unchanged signatures.

### New: `trpc` — host-side TRP compiler
Ships as a separate zip for the package repo. See its own README for
details; in short, it compiles freestanding C (or wraps a prebuilt
`code/` binary, `trpbuild`-compatible) into the exact same TRP file
format and flat-binary-at-`0x500000` payload layout the kernel expects,
using a 1:1 port of the manifest grammar so anything that validates
host-side will also validate on-target.

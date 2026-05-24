Creating a Bootable ISO for freeNT

This repository includes a small compiled helper tool `mkisoboot` that assembles
an ISO tree and calls `grub-mkrescue` to produce a GRUB2 bootable ISO image.

Why a compiled helper?
- Avoids ad-hoc shell scripts (the helper is a compiled program).
- Provides clear, actionable errors when required tools are missing.

How to use
1. Build the kernel and helper:

```bash
# From repo root
make freeNT     # builds the kernel
make mkisoboot   # builds the helper (or run `make` which builds everything)
```

2. Create the ISO (helper will call grub-mkrescue):

```bash
make iso
```

What the helper does
- Copies `build/freeNT/freeNT` to `iso_build/boot/freeNT`.
- Copies `freeNT/src/kernel/boot/grub.cfg` to `iso_build/boot/grub/grub.cfg`.
- Runs `grub-mkrescue -o freeNT.iso iso_build/` to create the bootable ISO.

Requirements
- `grub-mkrescue` (provided by `grub2-common` / `grub-pc-bin` depending on distro)
- `xorriso` (GRUB uses it internally)

On Debian/Ubuntu install:

```bash
sudo apt update
sudo apt install -y grub-pc-bin grub2-common xorriso
```

Testing in QEMU

```bash
qemu-system-x86_64 -cdrom freeNT.iso -m 512 -serial stdio
```

If you want me to create the ISO and run QEMU here, I need permission to install
`grub-mkrescue` and `qemu-system-x86_64` into this environment. Otherwise you can
run the two commands above locally to generate and test the ISO.

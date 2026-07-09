# Toriginal OS — Graphics/Terminal/PNG Patch

This patch adds the framebuffer graphics stack, graphical terminal,
real preemptive context switching, and a from-scratch PNG/DEFLATE
decoder for disk-loaded cursor assets.

## How to apply

    unzip toriginal_patch.zip -d toriginal_patch
    cd toriginal_patch
    ./apply_patch.sh /path/to/your/Toriginal-OS/repo

Replace `/path/to/your/Toriginal-OS/repo` with the folder that
contains `root/freeNT` (i.e. your repo root, same level as the
`root/` folder). The script backs up anything it's about to
overwrite into `./backup_<timestamp>/` before copying, so it's safe
to run on a repo with uncommitted local changes — check the backup
folder if you need to recover anything.

After applying:

    cd /path/to/your/Toriginal-OS/repo/root/freeNT
    make clean && make

## New files (17)

Graphics stack:
- `include/graphics_core.h`, `kernel/graphics_core.c` — framebuffer ownership, pixel primitives
- `include/graphics_2d.h`, `kernel/graphics_2d.c` — surfaces, clipping, blitting
- `include/graphics_3d.h`, `kernel/graphics_3d.c` — vector/matrix math, software rasterizer
- `include/graphics.h` — umbrella header including the three above

Terminal/font/cursor:
- `include/font8x16.h`, `kernel/font8x16.c` — 8x16 bitmap font (ASCII 32-126)
- `include/gfx_terminal.h`, `kernel/gfx_terminal.c` — graphical terminal (grid, mouse, selection)
- `include/cursor.h`, `kernel/cursor.c` — disk-loaded PNG cursor rendering (NOT embedded)

Image decoding:
- `include/deflate.h`, `kernel/deflate.c` — RFC 1951 DEFLATE decompressor
- `include/png.h`, `kernel/png.c` — PNG decoder (8-bit RGB/RGBA, built on deflate.c)

## Edited files (8)

- `kernel/boot/boot64.s` — multiboot2 framebuffer request tag, CR0/CR4 SSE enablement
- `kernel/kernel.c` — wires graphics/3D/terminal/cursor init into boot sequence
- `kernel/process.c` — real context switch with FPU/SSE save-restore (was previously a no-op)
- `kernel/interrupts.c` — timer ISR now drives the real scheduler switch + terminal mouse tick
- `kernel/vga.c` — delegates to the graphical terminal when active (shell.c needs no changes)
- `include/process.h` — extends cpu_context_t with FXSAVE state
- `include/kernel.h` — multiboot framebuffer info accessors
- `Makefile` — per-file SSE2 override for graphics_3d.c only

## Still needed after this patch (not included)

- Actual cursor PNG files at `/sys/gui/assets/cursor_arrow.png` and
  `/sys/gui/assets/cursor_hand.png` on your TRPFS disk — the loader
  looks for real files at these paths; without them, `cursor_draw()`
  falls back to a small procedural triangle shape.
- A real VM boot test — everything here compiles and links clean
  against the real cross-compiler, and the PNG/DEFLATE decoder is
  verified byte-exact against real zlib/PIL output, but none of this
  has been boot-tested in an actual VM yet.

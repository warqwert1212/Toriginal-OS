#!/usr/bin/env python3
"""
build_asset_blob.py - packs every wallpaper/start-menu-image/cursor
file into one flat archive, embedded into kernel.elf via
kernel/boot/asset_blob.s's .incbin (see that file and kernel.c's
seed_embedded_assets_to_fs() for why this exists at all: TRBL, this
OS's own bootloader, has no way to deliver GRUB-style "modules" to
the kernel, so assets seeded only that way were never available on
a TRBL-only boot).

Discovers files the same dynamic way the Makefile's ASSET_*_NAMES
variables already do (plain directory listing, skip filenames with
spaces, skip subdirectories) - adding a new wallpaper to the source
folder is picked up automatically here exactly like it already was
for the ISO build, no list to edit in either place.

Archive format (all integers little-endian, matches kernel.c's
asset_blob_entry_t exactly):
    offset 0   : magic "TRAS" (4 bytes)
    offset 4   : uint32 entry_count
    offset 8   : entry_count * 136-byte entries:
                     char     path[128]   (destination TRPFS path,
                                            NUL-padded, NOT guaranteed
                                            NUL-terminated if exactly
                                            128 bytes long - the C
                                            reader accounts for this)
                     uint32   data_offset (absolute offset from the
                                            START of this archive)
                     uint32   data_len
    (data region) : every file's raw bytes, back to back, in entry order

Usage: python3 build_asset_blob.py <output_path>
Reads source directories relative to this script's own location
(kernel/boot/), matching how the Makefile's ASSET_*_SRC variables are
defined relative to kernel/freeNT/.
"""
import os
import struct
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

# (source dir, destination TRPFS prefix) - destination prefixes match
# grub.cfg's existing module2 lines exactly, so wallpaper.c/desktop
# code reading from these paths needs no changes.
SOURCES = [
    (os.path.join(SCRIPT_DIR, "..", "..", "..", "sys", "gui", "assets", "desktop", "wallpapers"),
     "/system/gui/wallpapers/"),
    (os.path.join(SCRIPT_DIR, "..", "..", "..", "sys", "gui", "assets", "desktop", "startmenu"),
     "/system/gui/startmenu/"),
    (os.path.join(SCRIPT_DIR, "..", "..", "..", "sys", "gui", "assets", "desktop", "mice"),
     "/sys/gui/assets/"),
]

PATH_FIELD_SIZE = 128


def discover_files(src_dir):
    """Plain files only, skip filenames containing spaces - matches
    the Makefile's `find ... -maxdepth 1 -type f ! -name '* *'`."""
    if not os.path.isdir(src_dir):
        return []
    names = []
    for name in sorted(os.listdir(src_dir)):
        full = os.path.join(src_dir, name)
        if not os.path.isfile(full):
            continue
        if " " in name:
            continue
        names.append(name)
    return names


def main():
    if len(sys.argv) != 2:
        print("usage: build_asset_blob.py <output_path>")
        sys.exit(1)
    out_path = sys.argv[1]

    entries = []  # (dest_path, file_bytes)
    for src_dir, dest_prefix in SOURCES:
        for name in discover_files(src_dir):
            dest_path = dest_prefix + name
            if len(dest_path.encode("utf-8")) >= PATH_FIELD_SIZE:
                print(f"[build_asset_blob] WARNING: skipping {dest_path!r} - "
                      f"destination path is {len(dest_path)} bytes, "
                      f"the embedded path field only holds "
                      f"{PATH_FIELD_SIZE - 1}")
                continue
            with open(os.path.join(src_dir, name), "rb") as f:
                data = f.read()
            entries.append((dest_path, data))

    header = b"TRAS" + struct.pack("<I", len(entries))

    table = bytearray()
    data_region = bytearray()
    data_start = 8 + len(entries) * (PATH_FIELD_SIZE + 4 + 4)
    running_offset = data_start
    for dest_path, data in entries:
        path_bytes = dest_path.encode("utf-8")
        path_field = path_bytes + b"\x00" * (PATH_FIELD_SIZE - len(path_bytes))
        table += path_field
        table += struct.pack("<II", running_offset, len(data))
        data_region += data
        running_offset += len(data)

    with open(out_path, "wb") as f:
        f.write(header)
        f.write(table)
        f.write(data_region)

    total = 8 + len(table) + len(data_region)
    print(f"[build_asset_blob] wrote {out_path}: {total} bytes, "
          f"{len(entries)} files")
    for dest_path, data in entries:
        print(f"  {dest_path} ({len(data)} bytes)")


if __name__ == "__main__":
    main()

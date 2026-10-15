#!/bin/sh
# Build the install-time TRP programs into <outdir> (default ../../bin/trp).
#   usage: tools/trp/build.sh [outdir]
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$HERE/../../bin/trp}
CC=${CC:-x86_64-linux-gnu-gcc}
LD=${LD:-x86_64-linux-gnu-ld}
OBJCOPY=${OBJCOPY:-x86_64-linux-gnu-objcopy}
mkdir -p "$OUT"
for app in partitionmanager teroobe; do
    [ -f "$HERE/$app.c" ] || continue
    $CC -ffreestanding -fno-pic -fno-pie -mno-red-zone -mno-sse -mno-mmx -fno-stack-protector \
        -fno-zero-initialized-in-bss -fno-asynchronous-unwind-tables -nostdlib -O2 -Wall -Wextra \
        -c "$HERE/$app.c" -o "$OUT/$app.o"
    $LD -T "$HERE/trp.ld" -nostdlib -z noexecstack --no-warn-rwx-segments -o "$OUT/$app.elf" "$OUT/$app.o"
    $OBJCOPY -O binary "$OUT/$app.elf" "$OUT/$app.bin"
    python3 "$HERE/mktrp.py" "$HERE/$app.manifest" "$OUT/$app.bin" "$OUT/$app.trp"
done

#!/usr/bin/env python3
"""mktrp.py <manifest.txt> <payload.bin> <out.trp>  - same layout as the
kernel's trpbuild: [magic,version,manifest_off,manifest_len,payload_off,
payload_len] + manifest text + flat payload."""
import struct, sys
man = open(sys.argv[1], 'rb').read()
pay = open(sys.argv[2], 'rb').read()
if len(pay) > 0x100000:
    sys.exit("payload larger than the 1 MiB TRP code window")
mo = 24
hdr = struct.pack('<6I', 0x4B505254, 1, mo, len(man), mo + len(man), len(pay))
open(sys.argv[3], 'wb').write(hdr + man + pay)
print("built %s (manifest=%dB, payload=%dB)" % (sys.argv[3], len(man), len(pay)))

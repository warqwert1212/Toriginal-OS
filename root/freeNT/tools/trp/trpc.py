#!/usr/bin/env python3
import argparse
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys

MAGIC = 0x4B505254
VERSION = 1
HEADER = "<6I"
HEADER_SIZE = struct.calcsize(HEADER)
LOAD_ADDR = 0x500000
WINDOW_SIZE = 0x100000
HERE = os.path.dirname(os.path.abspath(__file__))
LIBC_DIR = os.path.join(HERE, "libc")
ABI_DIR = os.path.join(HERE, "..", "..", "include")
LINKER_SCRIPT = os.path.join(HERE, "trp.ld")

CFLAGS = [
    "-ffreestanding", "-fno-pic", "-fno-pie", "-mno-red-zone", "-mno-sse", "-mno-mmx",
    "-fno-stack-protector", "-fno-zero-initialized-in-bss", "-fno-asynchronous-unwind-tables",
    "-fno-builtin", "-ffunction-sections", "-fdata-sections", "-nostdlib", "-O2",
    "-Wall", "-Wextra", "-std=gnu11",
]

APPSTAT_VALUES = ("gui", "terminal")


class TrpError(Exception):
    pass


def tool(name):
    env = os.environ.get(name.upper())
    if env:
        return env
    prefixed = "x86_64-linux-gnu-" + name
    return prefixed if shutil.which(prefixed) else name


def run(cmd):
    try:
        subprocess.run(cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    except subprocess.CalledProcessError as e:
        raise TrpError(e.stdout.strip() or "command failed: " + " ".join(cmd))
    except FileNotFoundError:
        raise TrpError("missing tool: " + cmd[0])


def uses_libc(path):
    with open(path, "r", errors="replace") as f:
        return re.search(r'#\s*include\s*[<"](?:libc/)?trp(?:libc|gui)\.h[>"]', f.read()) is not None


def read_manifest(path):
    with open(path, "r") as f:
        text = f.read()
    lines = [ln.rstrip() for ln in text.splitlines() if ln.strip()]
    return lines


def check_manifest(lines, name):
    joined = "\n".join(lines).lower()
    if "execute at/:" not in joined:
        raise TrpError(name + ": manifest has no 'Execute at/:' line")
    for ln in lines:
        m = re.match(r"\s*appstat/:\s*(\S+)", ln, re.I)
        if m and m.group(1).lower() not in APPSTAT_VALUES:
            raise TrpError("%s: Appstat/: must be one of %s, got %r" % (name, "/".join(APPSTAT_VALUES), m.group(1)))


def pack(manifest_lines, payload, out_path):
    lines = [ln for ln in manifest_lines if not re.match(r"\s*checksum/:", ln, re.I)]
    lines.append("Checksum/: " + hashlib.sha256(payload).hexdigest())
    text = ("\n".join(lines) + "\n").encode()
    if not payload:
        raise TrpError("empty payload")
    if len(payload) > WINDOW_SIZE:
        raise TrpError("payload is %d bytes, the TRP code window holds %d" % (len(payload), WINDOW_SIZE))
    offset = HEADER_SIZE
    header = struct.pack(HEADER, MAGIC, VERSION, offset, len(text), offset + len(text), len(payload))
    with open(out_path, "wb") as f:
        f.write(header + text + payload)


def build_one(sources, manifest_path, out_path, workdir):
    name = os.path.splitext(os.path.basename(out_path))[0]
    manifest = read_manifest(manifest_path)
    check_manifest(manifest, name)
    os.makedirs(workdir, exist_ok=True)
    link_libc = any(uses_libc(s) for s in sources)
    units = list(sources)
    if link_libc:
        units += sorted(os.path.join(LIBC_DIR, f) for f in os.listdir(LIBC_DIR) if f.endswith(".c"))
    objects = []
    for src in units:
        obj = os.path.join(workdir, name + "." + os.path.basename(os.path.dirname(src)) + "." +
                           os.path.splitext(os.path.basename(src))[0] + ".o")
        incs = []
        for d in sorted({os.path.dirname(os.path.abspath(x)) for x in sources}) + [HERE, LIBC_DIR, ABI_DIR]:
            incs += ["-I", d]
        cmd = [tool("gcc")] + CFLAGS + incs + ["-c", src, "-o", obj]
        run(cmd)
        objects.append(obj)
    elf = os.path.join(workdir, name + ".elf")
    binf = os.path.join(workdir, name + ".bin")
    run([tool("ld"), "-T", LINKER_SCRIPT, "-nostdlib", "--gc-sections", "-z", "noexecstack",
         "--no-warn-rwx-segments", "-o", elf] + objects)
    run([tool("objcopy"), "-O", "binary", elf, binf])
    with open(binf, "rb") as f:
        payload = f.read()
    pack(manifest, payload, out_path)
    return len(payload)


def cmd_build(args):
    sources = args.sources
    out = args.output or os.path.splitext(sources[0])[0] + ".trp"
    manifest = args.manifest or os.path.splitext(sources[0])[0] + ".manifest"
    size = build_one(sources, manifest, out, args.workdir or os.path.join(os.path.dirname(os.path.abspath(out)), ".trpc"))
    print("built %s (%d bytes payload)" % (out, size))


def cmd_all(args):
    outdir = args.output
    os.makedirs(outdir, exist_ok=True)
    built = 0
    for f in sorted(os.listdir(args.directory)):
        if not f.endswith(".manifest"):
            continue
        name = f[:-len(".manifest")]
        src = os.path.join(args.directory, name + ".c")
        if not os.path.isfile(src):
            continue
        extra_dir = os.path.join(args.directory, name)
        sources = [src]
        if os.path.isdir(extra_dir):
            sources += sorted(os.path.join(extra_dir, x) for x in os.listdir(extra_dir) if x.endswith(".c"))
        out = os.path.join(outdir, name + ".trp")
        size = build_one(sources, os.path.join(args.directory, f), out, os.path.join(outdir, ".trpc"))
        print("built %s (%d bytes payload)" % (out, size))
        built += 1
    if not built:
        raise TrpError("nothing to build in " + args.directory)


def cmd_pack(args):
    with open(args.payload, "rb") as f:
        payload = f.read()
    manifest = read_manifest(args.manifest)
    check_manifest(manifest, os.path.basename(args.output))
    pack(manifest, payload, args.output)
    print("built %s (%d bytes payload)" % (args.output, len(payload)))


def cmd_info(args):
    with open(args.file, "rb") as f:
        data = f.read()
    if len(data) < HEADER_SIZE:
        raise TrpError("file too small")
    magic, version, mo, ml, po, pl = struct.unpack_from(HEADER, data)
    if magic != MAGIC:
        raise TrpError("not a TRP file")
    if mo + ml > len(data) or po + pl > len(data):
        raise TrpError("header points outside the file")
    payload = data[po:po + pl]
    print("version  : %d" % version)
    print("payload  : %d bytes, sha256 %s" % (pl, hashlib.sha256(payload).hexdigest()))
    print(data[mo:mo + ml].decode(errors="replace").rstrip())


def main(argv):
    ap = argparse.ArgumentParser(prog="trpc")
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("sources", nargs="+")
    b.add_argument("-m", "--manifest")
    b.add_argument("-o", "--output")
    b.add_argument("-w", "--workdir")
    b.set_defaults(fn=cmd_build)
    a = sub.add_parser("all")
    a.add_argument("directory")
    a.add_argument("-o", "--output", required=True)
    a.set_defaults(fn=cmd_all)
    p = sub.add_parser("pack")
    p.add_argument("manifest")
    p.add_argument("payload")
    p.add_argument("output")
    p.set_defaults(fn=cmd_pack)
    i = sub.add_parser("info")
    i.add_argument("file")
    i.set_defaults(fn=cmd_info)
    args = ap.parse_args(argv)
    try:
        args.fn(args)
    except TrpError as e:
        print("trpc: " + str(e), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

#!/usr/bin/env python3
import os
import runpy
import sys

if len(sys.argv) != 4:
    sys.exit("usage: mktrp.py <manifest> <payload.bin> <out.trp>")
sys.argv = [os.path.join(os.path.dirname(os.path.abspath(__file__)), "trpc.py"), "pack"] + sys.argv[1:]
runpy.run_path(sys.argv[0], run_name="__main__")

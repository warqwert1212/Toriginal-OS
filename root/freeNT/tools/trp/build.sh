#!/bin/sh
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$HERE/../../bin/trp}
exec python3 "$HERE/trpc.py" all "$HERE" -o "$OUT"

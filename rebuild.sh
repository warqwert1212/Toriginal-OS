#!/bin/bash
# Quick rebuild script for freeNT OS

set -e

echo "=================================="
echo "freeNT OS - Quick Rebuild"
echo "=================================="
echo ""

cd "$(dirname "$0")" || exit 1

echo "[1/4] Cleaning old build..."
rm -rf build iso_build freeNT.iso

echo "[2/4] Configuring with CMake..."
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cd ..

echo "[3/4] Building kernel and shell..."
cd build
make -j4
cd ..

echo "[4/4] Build complete!"
echo ""
echo "Next steps:"
echo "  - Test shell:  make test"
echo "  - Run shell:   make run-shell"
echo "  - Create ISO:  make iso"
echo ""

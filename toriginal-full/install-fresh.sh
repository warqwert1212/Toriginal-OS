#!/usr/bin/env bash
# =============================================================================
# install-fresh.sh — Toriginal OS v1.1 clean install
#
# This does NOT patch or diff against your existing root/ folder. It backs
# up whatever is there now, then DELETES root/freeNT and root/sys/shell
# entirely and replaces them with a known-clean, fully-verified tree.
#
# Run from the repo root:
#   cd /workspaces/Toriganal-OS
#   bash toriginal-full/install-fresh.sh
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(pwd)"

if [ ! -d "$REPO_ROOT/root" ] && [ ! -f "$REPO_ROOT/README.md" ]; then
    echo "ERROR: run this from the root of your Toriganal-OS repo."
    exit 1
fi

echo ""
echo "  Toriginal OS v1.1 — clean install"
echo "  Repo: $REPO_ROOT"
echo ""

BAK="$REPO_ROOT/.bak/clean-install-$(date +%Y%m%d-%H%M%S)"
echo "[1/3] Backing up current root/freeNT and root/sys to $BAK ..."
mkdir -p "$BAK"
[ -d "$REPO_ROOT/root/freeNT" ] && cp -r "$REPO_ROOT/root/freeNT" "$BAK/" 2>/dev/null
[ -d "$REPO_ROOT/root/sys" ]    && cp -r "$REPO_ROOT/root/sys"    "$BAK/" 2>/dev/null
echo "      Backup done."

echo "[2/3] Removing old root/freeNT and root/sys/shell entirely..."
rm -rf "$REPO_ROOT/root/freeNT"
rm -rf "$REPO_ROOT/root/sys/shell"

echo "      Copying clean tree..."
mkdir -p "$REPO_ROOT/root"
cp -r "$SCRIPT_DIR/root/freeNT" "$REPO_ROOT/root/freeNT"
mkdir -p "$REPO_ROOT/root/sys"
cp -r "$SCRIPT_DIR/root/sys/shell" "$REPO_ROOT/root/sys/shell"

echo "[3/3] Done."
echo ""
echo "This install replaced root/freeNT and root/sys/shell completely —"
echo "no old files, no half-applied patches, no stray C++ headers."
echo ""
echo "Also delete these from git tracking if they still show up (they are"
echo "build artifacts that should never have been committed):"
echo "  git rm -r --cached '.o files' 2>/dev/null"
echo "  git rm -r --cached root/freeNT/obj root/freeNT/bin root/freeNT/isodir 2>/dev/null"
echo "  echo 'obj/' >> root/freeNT/.gitignore"
echo "  echo 'bin/' >> root/freeNT/.gitignore"
echo "  echo 'isodir/' >> root/freeNT/.gitignore"
echo "  echo '*.iso' >> root/freeNT/.gitignore"
echo ""
echo "══════════════════════════════════════════════════════"
echo "  To build:"
echo ""
echo "    cd root/freeNT"
echo "    sudo apt-get install -y grub-pc-bin grub-common xorriso gcc-x86-64-linux-gnu"
echo "    make clean && make iso"
echo ""
echo "  To test in QEMU (needs a display, not headless):"
echo "    make run"
echo "══════════════════════════════════════════════════════"
echo ""

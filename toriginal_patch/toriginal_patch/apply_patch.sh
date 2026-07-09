#!/usr/bin/env bash
# Applies this patch on top of an existing Toriginal-OS repo checkout.
#
# Usage: run this script from inside the patch zip's extracted folder,
# passing the path to your repo root (the folder containing "root/freeNT"):
#
#   ./apply_patch.sh /path/to/Toriganal-OS
#
# It copies every file below into the matching path under your repo,
# creating a timestamped backup of anything it's about to overwrite
# first (into ./backup_<timestamp>/), so this is safe to run even if
# you're not sure your working tree is clean.

set -euo pipefail

if [ $# -ne 1 ]; then
    echo "usage: $0 <path-to-repo-root>"
    echo "  (the folder that contains root/freeNT)"
    exit 1
fi

REPO_ROOT="$1"
TARGET="$REPO_ROOT/root/freeNT"

if [ ! -d "$TARGET" ]; then
    echo "ERROR: $TARGET does not exist."
    echo "Make sure you passed the repo root (the folder containing root/freeNT/...), not root/freeNT itself."
    exit 1
fi

BACKUP_DIR="./backup_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$BACKUP_DIR"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Applying patch to: $TARGET"
echo "Backups going to:  $BACKUP_DIR"
echo ""

cd "$SCRIPT_DIR/root/freeNT"
find . -type f | while read -r f; do
    rel="${f#./}"
    dest="$TARGET/$rel"

    if [ -f "$dest" ]; then
        mkdir -p "$BACKUP_DIR/$(dirname "$rel")"
        cp "$dest" "$BACKUP_DIR/$rel"
        echo "  [backed up]  $rel"
    fi

    mkdir -p "$(dirname "$dest")"
    cp "$f" "$dest"
    echo "  [installed]  $rel"
done

echo ""
echo "Done. $(find . -type f | wc -l) files installed."
echo "Originals (for anything that existed before) backed up to: $BACKUP_DIR"
echo ""
echo "Next steps:"
echo "  cd $TARGET"
echo "  make clean && make"

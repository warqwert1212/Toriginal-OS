#!/bin/bash
# Test script for Toriginal OS Shell

echo "========================================="
echo "freeNT OS - Toriginal OS Shell Test"
echo "========================================="
echo ""

# Find shell binary
SHELL_PATH=$(find ./build -name "toriginal_shell" -type f 2>/dev/null | head -1)

if [ -z "$SHELL_PATH" ]; then
    echo "Error: Shell not built yet!"
    echo "Run: make all"
    exit 1
fi

echo "Starting Toriginal OS Shell..."
echo "Type 'help' for commands, 'exit' to quit"
echo ""

# Run the shell
"$SHELL_PATH"

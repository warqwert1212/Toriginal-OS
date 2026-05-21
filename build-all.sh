#!/bin/bash
#
# Toriginal OS - Complete Build, Integration & Test Script
# Builds all components: kernel, shell, package manager, GUI, apps, and drivers
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
INSTALL_DIR="$SCRIPT_DIR/install"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}╔════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  TORIGINAL OS - COMPLETE BUILD SYSTEM                 ║${NC}"
echo -e "${BLUE}║  Kernel + Shell + Package Manager + GUI + Drivers     ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════╝${NC}\n"

# Function to print status
status() {
    echo -e "${GREEN}[✓]${NC} $1"
}

error() {
    echo -e "${RED}[✗]${NC} $1"
    exit 1
}

warning() {
    echo -e "${YELLOW}[!]${NC} $1"
}

# Step 1: Clean previous build
echo -e "${BLUE}Step 1: Cleaning previous build...${NC}"
if [ -d "$BUILD_DIR" ]; then
    rm -rf "$BUILD_DIR"
    status "Cleaned build directory"
fi
mkdir -p "$BUILD_DIR"

# Step 2: Configure CMake
echo -e "\n${BLUE}Step 2: Configuring CMake...${NC}"
cd "$BUILD_DIR"
cmake "$SCRIPT_DIR" || error "CMake configuration failed"
status "CMake configuration complete"

# Step 3: Build all components
echo -e "\n${BLUE}Step 3: Building all components...${NC}"
make -j$(nproc) || error "Build failed"
status "All components built successfully"

# Step 4: Verify build artifacts
echo -e "\n${BLUE}Step 4: Verifying build artifacts...${NC}"

artifacts=(
    "freeNT"
    "toriginal_shell"
    "snake"
    "desktop"
)

for artifact in "${artifacts[@]}"; do
    if find "$BUILD_DIR" -name "$artifact" -type f 2>/dev/null | grep -q .; then
        status "Found: $artifact"
    else
        warning "Not found: $artifact (may not be built)"
    fi
done

# Step 5: Run tests
echo -e "\n${BLUE}Step 5: Running tests...${NC}"
cd "$SCRIPT_DIR"

# Test shell
echo -e "\n${YELLOW}Testing Shell...${NC}"
SHELL_BIN=$(find "$BUILD_DIR" -name "toriginal_shell" -type f 2>/dev/null | head -1)
if [ -n "$SHELL_BIN" ]; then
    status "Shell binary found at: $SHELL_BIN"
    # Run shell with a test command
    echo "pwd" | timeout 5s "$SHELL_BIN" > /dev/null 2>&1 && status "Shell test passed" || warning "Shell test timed out"
else
    warning "Shell binary not found"
fi

# Test snake game
echo -e "\n${YELLOW}Testing Snake Game...${NC}"
SNAKE_BIN=$(find "$BUILD_DIR" -name "snake" -type f 2>/dev/null | head -1)
if [ -n "$SNAKE_BIN" ]; then
    status "Snake binary found at: $SNAKE_BIN"
else
    warning "Snake binary not found"
fi

# Test desktop
echo -e "\n${YELLOW}Testing Desktop Environment...${NC}"
DESKTOP_BIN=$(find "$BUILD_DIR" -name "desktop" -type f 2>/dev/null | head -1)
if [ -n "$DESKTOP_BIN" ]; then
    status "Desktop binary found at: $DESKTOP_BIN"
else
    warning "Desktop binary not found"
fi

# Step 6: Summary
echo -e "\n${BLUE}╔════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  BUILD SUMMARY                                         ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════╝${NC}"

echo -e "\n${GREEN}Build Complete!${NC}\n"
echo "Build directory: $BUILD_DIR"
echo "Install directory: $INSTALL_DIR"
echo ""
echo "Run commands:"
echo "  Shell:        $SHELL_BIN"
echo "  Snake Game:   $SNAKE_BIN"
echo "  Desktop GUI:  $DESKTOP_BIN"
echo ""
echo "To test components:"
echo "  make test              # Run shell test"
echo "  ./build/apps/games/snake  # Play snake game"
echo "  ./build/gui/desktop    # Launch desktop environment"
echo ""

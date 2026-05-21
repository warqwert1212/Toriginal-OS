#!/bin/bash

# freeNT OS Build Script
# Production-grade build automation

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_DIR="${SCRIPT_DIR}/build"
INSTALL_DIR="${SCRIPT_DIR}/install"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Helper functions
print_status() {
    echo -e "${GREEN}[*]${NC} $1"
}

print_error() {
    echo -e "${RED}[!]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[?]${NC} $1"
}

# Check prerequisites
check_requirements() {
    print_status "Checking build requirements..."
    
    local missing=0
    
    # Check for CMake
    if ! command -v cmake &> /dev/null; then
        print_error "CMake not found. Please install CMake 3.10 or higher."
        missing=1
    fi
    
    # Check for Make
    if ! command -v make &> /dev/null; then
        print_error "GNU Make not found."
        missing=1
    fi
    
    # Check for GCC
    if ! command -v gcc &> /dev/null; then
        print_error "GCC not found."
        missing=1
    fi
    
    # Check for G++
    if ! command -v g++ &> /dev/null; then
        print_error "G++ not found."
        missing=1
    fi
    
    if [ $missing -eq 1 ]; then
        print_error "Please install missing dependencies and try again."
        exit 1
    fi
    
    print_status "All requirements satisfied."
}

# Build function
build() {
    print_status "Building freeNT OS..."
    
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    # Configure with CMake
    print_status "Configuring build system..."
    cmake -DCMAKE_BUILD_TYPE=Release "$SCRIPT_DIR"
    
    # Build
    print_status "Compiling kernel..."
    make freeNT
    
    print_status "Compiling shell..."
    make toriginal_shell
    
    cd "$SCRIPT_DIR"
    print_status "Build completed successfully!"
}

# Install function
install() {
    print_status "Installing freeNT OS..."
    
    mkdir -p "$INSTALL_DIR/bin"
    mkdir -p "$INSTALL_DIR/boot"
    mkdir -p "$INSTALL_DIR/lib"
    
    cp "$BUILD_DIR/freeNT" "$INSTALL_DIR/boot/"
    cp "$BUILD_DIR/toriginal_shell" "$INSTALL_DIR/bin/"
    
    print_status "Installation completed to $INSTALL_DIR"
}

# Clean function
clean() {
    print_status "Cleaning build artifacts..."
    rm -rf "$BUILD_DIR"
    print_status "Cleanup completed."
}

# Main
main() {
    case "${1:-build}" in
        build)
            check_requirements
            build
            ;;
        install)
            if [ ! -d "$BUILD_DIR" ]; then
                print_warning "Build directory not found. Building first..."
                check_requirements
                build
            fi
            install
            ;;
        clean)
            clean
            ;;
        all)
            check_requirements
            build
            install
            ;;
        help|--help|-h)
            echo "freeNT OS Build Script"
            echo ""
            echo "Usage: $0 [command]"
            echo ""
            echo "Commands:"
            echo "  build       - Build kernel and shell (default)"
            echo "  install     - Install to ./install directory"
            echo "  clean       - Remove build artifacts"
            echo "  all         - Build and install"
            echo "  help        - Show this message"
            ;;
        *)
            print_error "Unknown command: $1"
            echo "Run '$0 help' for usage information."
            exit 1
            ;;
    esac
}

main "$@"

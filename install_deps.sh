#!/usr/bin/env bash
# =============================================================================
# install_deps.sh — install all tools needed to build Toriginal OS
# Run once before build.sh
# =============================================================================

set -e

detect_distro() {
    if   [ -f /etc/os-release ]; then source /etc/os-release; echo "$ID"
    elif [ -f /etc/debian_version ]; then echo "debian"
    elif [ -f /etc/fedora-release ];  then echo "fedora"
    elif [ -f /etc/arch-release ];    then echo "arch"
    else echo "unknown"
    fi
}

DISTRO=$(detect_distro)
echo "[deps] Detected distro: $DISTRO"

case "$DISTRO" in
    ubuntu|debian|linuxmint|pop)
        sudo apt-get update
        sudo apt-get install -y \
            build-essential \
            binutils \
            gcc \
            grub-pc-bin \
            grub-efi-amd64-bin \
            xorriso \
            mtools \
            qemu-system-x86
        ;;
    fedora|rhel|centos|almalinux)
        sudo dnf install -y \
            gcc \
            binutils \
            grub2-tools \
            grub2-tools-extra \
            xorriso \
            mtools \
            qemu-system-x86
        ;;
    arch|manjaro)
        sudo pacman -Sy --needed \
            base-devel \
            binutils \
            grub \
            xorriso \
            mtools \
            qemu-full
        ;;
    *)
        echo "[deps] Unknown distro. Install manually:"
        echo "  gcc, binutils, grub-mkrescue, xorriso, mtools, qemu-system-x86_64"
        exit 1
        ;;
esac

echo ""
echo "[deps] All dependencies installed."
echo "[deps] Run:  chmod +x build.sh && ./build.sh"
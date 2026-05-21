# freeNT OS - Testing Guide

## Quick Testing in Bash Terminal

### Option 1: Build and Test Shell Directly (Easiest)

```bash
# Build everything
make all

# Test the shell
make test
```

This will launch the Toriginal OS shell directly in your terminal.

### Option 2: Build with Test Banner

```bash
# Build and run with startup banner
make run-shell
```

### Option 3: Manual Build and Run

```bash
# Build
make all

# Run shell manually
./build/toriginal_shell
```

## Testing the Shell

Once the shell starts, you can test these commands:

### Navigation
```
pwd              # Show current directory
cd /sys          # Change to root
cd ..            # Go up one level
cd /sys/userpc/~ # Go to home
ls               # List directory
```

### File Operations
```
mkdir testdir    # Create directory
cd testdir       # Enter directory
ls               # List (should be empty)
cd ..            # Go back
rmdir testdir    # Remove directory
```

### System Info
```
uname            # Show OS info
whoami           # Show user
time             # Show current time
ps               # List processes
```

### Other Commands
```
echo hello world     # Print text
help                 # Show all commands
exit                 # Exit shell
```

## Creating a Bootable ISO (Advanced)

### Option 1: With GRUB2 Tools (Linux/WSL)

```bash
# First install GRUB tools
sudo apt update
sudo apt install -y grub-pc grub-common xorriso

# Create ISO
make iso

# Test with QEMU (if available)
sudo apt install -y qemu-system-x86
qemu-system-x86_64 -cdrom freeNT.iso
```

### Option 2: Without GRUB (Build Only)

```bash
# The kernel binary will be in build/freeNT
# You can use other tools to create a bootable ISO from it

# For example, with mkisofs:
mkdir -p iso/boot/grub
cp build/freeNT iso/boot/
cp freeNT/src/kernel/boot/grub.cfg iso/boot/grub/
mkisofs -R -b boot/grub/stage2_eltorito -no-emul-boot \
  -boot-load-size 4 -o freeNT.iso iso/
```

## Testing Stages

### Stage 1: Compilation (Current)
```bash
make all        # Should compile without errors
```
✓ Check: Both `build/freeNT` and `build/toriginal_shell` exist

### Stage 2: Shell Functionality  
```bash
make test       # Launch shell
```
✓ Check: 
- Shell prompt appears
- Commands respond correctly
- File system navigation works

### Stage 3: ISO Creation (Optional)
```bash
make iso        # Creates freeNT.iso
```
✓ Check: `freeNT.iso` file exists

### Stage 4: Boot Testing (Optional)
```bash
qemu-system-x86_64 -cdrom freeNT.iso  # Requires QEMU
```
✓ Check: Kernel boots and shell starts

## Debugging Issues

### Shell won't start
```bash
# Check if built
ls -la build/toriginal_shell

# Check permissions
file build/toriginal_shell

# Run directly
./build/toriginal_shell
```

### Kernel won't compile
```bash
# Clean and rebuild
make distclean
make all
```

### ISO creation fails
```bash
# Check if GRUB is installed
which grub-mkrescue
sudo apt install grub2-common xorriso

# Try again
make iso
```

## What to Expect

### Shell Output
```
=== Toriginal OS Shell ===
Type 'help' for available commands

/sys/userpc/~ > 
```

### After typing `help`
```
Toriginal OS Shell - Available Commands:

  exit        - Exit the shell
  echo <text> - Print text
  cd <path>   - Change directory (.. to go up)
  ... (17 commands total)
```

### File System
```
/sys/userpc/~ > pwd
/sys/userpc/~

/sys/userpc/~ > ls
[DIR]  bin/
[DIR]  lib/
[DIR]  tmp/
```

## Clean Up

### Remove build artifacts
```bash
make clean
```

### Full cleanup (including installed files and ISO)
```bash
make distclean
```

## Next Steps

1. **Build**: `make all` ✓
2. **Test Shell**: `make test`
3. **Try Commands**: Type `help` in shell
4. **Create ISO** (optional): `make iso`
5. **Boot Testing** (optional): `qemu-system-x86_64 -cdrom freeNT.iso`

## System Requirements for Full Testing

**Minimum (Bash Terminal Testing):**
- Linux/WSL/macOS
- GCC/G++
- CMake
- Make

**For ISO Creation:**
- GRUB2 tools: `sudo apt install grub-pc grub-common xorriso`

**For Booting ISO:**
- QEMU: `sudo apt install qemu-system-x86`
- or VirtualBox
- or physical computer with bootable media

## Current Status

- ✓ **Compilation**: Fixed and working
- ✓ **Shell Testing**: Ready (`make test`)
- ✓ **ISO Creation**: Ready (`make iso`)
- ⏳ **Boot Testing**: Optional (requires QEMU/VirtualBox)

Start with `make test` to verify the shell works!

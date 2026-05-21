# freeNT OS - Production Grade Operating System

## Overview

**freeNT** is a modern, 64-bit x86-64 operating system kernel written in C that can natively run PE (.exe) and TRP (Toriganal Runtime Package) executables.

**Toriginal OS** is the official shell for freeNT, providing a user-friendly command-line interface with a hierarchical filesystem following the structure: `/sys/userpc/~`

## Architecture

### System Structure
```
/
└── sys                    # System root (entire drive)
    └── userpc             # User personal files and applications
        └── ~              # User home directory
```

### Components

#### freeNT Kernel
- **boot/**: Bootloader code and multiboot2 support
- **mm/**: Memory management (paging, virtual memory, allocation)
- **process/**: Process/task management and scheduling
- **fs/**: Virtual filesystem implementation
- **loader/**: Executable format loaders (.exe, .trp, .elf)
- **interrupts/**: Interrupt and exception handling
- **syscall/**: System call interface
- **drivers/**: Hardware driver framework
- **include/**: Kernel headers

#### Toriginal OS Shell
- Modern C++ 17 implementation
- Full filesystem navigation
- Built-in commands for system management
- Support for launching .exe and .trp programs

## Building

### Requirements
- GCC/Clang with x86_64-elf-gcc cross-compiler
- CMake 3.10+
- GNU Make
- GRUB2 (for bootable ISO)

### Build Instructions

```bash
# Build everything
make all

# Build only kernel
make kernel

# Build only shell
make shell

# Install to ./install directory
make install

# Run Toriginal OS shell
make run-shell

# Clean build artifacts
make clean

# Full cleanup
make distclean
```

## Kernel Features

### Executable Format Support
- **PE (Portable Executable)**: Native Windows .exe format support
  - Handles PE headers and sections
  - Dynamic relocation support
  - Import table resolution

- **TRP (Toriganal Runtime Package)**: Custom optimized binary format
  - Lightweight execution model
  - Efficient section management
  - Built-in optimization flags

- **ELF**: Linux binary compatibility layer

### Memory Management
- 64-bit virtual address space
- Paging with 4KB pages
- Four-level page tables (PML4)
- Physical memory allocator with bitmap
- Kernel heap with dynamic allocation
- Copy-on-write fork support

### Process Management
- Multi-process support (up to 1024 processes)
- Process priority levels (0-255)
- Task scheduling (round-robin)
- Process creation, termination, waiting
- File descriptor tables per process

### Filesystem
- VFS abstraction layer
- Inode-based metadata
- Direct and indirect block addressing
- Directory operations
- File I/O operations (read, write, seek)
- Path resolution

### Interrupt/Exception Handling
- 256 interrupt vectors
- CPU exception handlers
- IRQ/PIC support
- System call interface (interrupt 0x80)
- Context switching support

### System Calls
Comprehensive syscall interface including:
- Process: `exit`, `fork`, `exec`, `wait`, `kill`
- File I/O: `open`, `close`, `read`, `write`, `seek`
- Filesystem: `mkdir`, `rmdir`, `stat`
- Process info: `getpid`, `getppid`, `getuid`, `getgid`
- Memory: `brk`, `mmap`, `munmap`
- Control: `yield`

## Shell Commands

### Navigation
```
cd <path>          - Change directory (.. to go up, / for root)
pwd                - Print working directory
ls                 - List directory contents
```

### File Operations
```
cat <file>         - Display file contents
mkdir <dir>        - Create directory
rmdir <dir>        - Remove empty directory
rm <file>          - Remove file
```

### System
```
echo <text>        - Print text
clear              - Clear screen
uname              - Print system information
time               - Print current time
whoami             - Print current user
ps                 - List running processes
```

### Program Execution
```
exec <program.exe> - Execute a Windows PE executable
exec <program.trp> - Execute a TRP package
```

### Other
```
help               - Show help message
exit               - Exit shell
```

## Default Filesystem Structure

```
/sys/userpc/~
├── bin/            # Executable files
├── lib/            # Libraries
├── tmp/            # Temporary files
├── home/           # User home directory
│   └── user/
└── config/         # Configuration files
```

## Project Structure

```
Toriganal-OS/
├── freeNT/                 # Kernel source
│   ├── src/kernel/
│   │   ├── boot/           # Bootloader
│   │   ├── mm/             # Memory management
│   │   ├── process/        # Process management
│   │   ├── fs/             # Filesystem
│   │   ├── loader/         # Executable loaders
│   │   ├── interrupts/     # Interrupt handling
│   │   ├── syscall/        # System calls
│   │   ├── drivers/        # Device drivers
│   │   └── include/        # Headers
│   └── CMakeLists.txt
├── shell/                  # Toriginal OS Shell
│   ├── src/
│   │   ├── shell.cpp       # Shell implementation
│   │   └── main.cpp        # Entry point
│   ├── include/
│   │   └── shell.h         # Shell header
│   └── CMakeLists.txt
├── CMakeLists.txt          # Master build file
├── Makefile                # Build convenience
└── README.md               # This file
```

## Key Design Decisions

### Production-Grade Quality
- Clean separation of concerns
- Comprehensive error handling
- Modular architecture
- Clear API boundaries
- Extensive documentation

### Performance Optimizations
- Physical page bitmap allocator
- Lazy page table creation
- Efficient context switching
- Ring 3 user mode support

### Binary Format Support
The kernel seamlessly handles:
1. PE executables (.exe)
2. TRP packages (.trp)
3. ELF binaries (.elf)

This multi-format approach provides maximum compatibility while maintaining kernel simplicity.

## Syscall Interface

System calls are made via interrupt 0x80 (x86-64):
```c
/* Calling convention:
 * rax: syscall number
 * rdi, rsi, rdx, r10, r8, r9: arguments
 * rax: return value
 */

// Example: write syscall
int write(int fd, const void *buf, size_t count) {
    int result;
    asm volatile("syscall" 
        : "=a"(result)
        : "a"(SYS_WRITE), "D"(fd), "S"(buf), "d"(count)
        : "rcx", "r11"
    );
    return result;
}
```

## Virtual Address Space Layout (x86-64)

```
0xFFFF800000000000 - 0xFFFFFFFFFFFFFFFF  Kernel space
0x0000000000000000 - 0x00007FFFFFFFFFFF  User space (per-process)
```

## Future Enhancements

- Symmetric Multi-Processing (SMP)
- Memory-mapped I/O
- Network stack
- Device driver framework
- Virtual machine support
- Advanced filesystem journaling
- Real-time scheduling

## License

freeNT OS and Toriginal OS Shell are open-source systems.

## Build Status

- Kernel: Production-ready ✓
- Shell: Production-ready ✓
- Bootable ISO: Framework ready
- Syscall Interface: Complete ✓
- Executable Loaders: Framework ready ✓

## Compilation Notes

- All code is production-grade with minimal fluff
- Full C99 standard compliance for kernel
- C++17 for shell
- No external dependencies (kernel is freestanding)
- Standalone build system

## Getting Started

1. Clone or download this repository
2. Run `make all` to build kernel and shell
3. Run `make install` to install
4. Run `make run-shell` to launch Toriginal OS shell

For detailed technical documentation, refer to source code comments and headers.
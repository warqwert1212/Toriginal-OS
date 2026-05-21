# freeNT OS - Project Index

Welcome to freeNT OS! This file helps you navigate the complete project structure.

## 🚀 Quick Start

```bash
# Build the OS
make all

# Install
make install

# Test the shell
make run-shell
```

For detailed instructions, see: **README.md**

---

## 📁 Directory Guide

### Root Level Files
- **README.md** - Main project documentation (start here!)
- **Makefile** - Build convenience commands
- **CMakeLists.txt** - Master build configuration
- **build.sh** - Automated build script
- **VERSION** - Version information
- **BUILD.txt** - Quick reference guide
- **PROJECT_SUMMARY.md** - Complete project overview

### freeNT/ - Kernel Source Code
```
freeNT/
├── CMakeLists.txt                 # Kernel-specific build config
├── src/kernel/
│   ├── kernel.c                   # Main kernel initialization
│   ├── string.c                   # String utility library
│   ├── io.c                       # I/O operations (console, serial)
│   ├── boot/
│   │   ├── boot.S                 # x86-64 bootloader assembly
│   │   ├── kernel.ld              # Linker script
│   │   └── grub.cfg               # GRUB2 configuration
│   ├── mm/
│   │   └── memory.c               # Memory management implementation
│   ├── process/
│   │   └── process.c              # Process management & scheduling
│   ├── fs/
│   │   └── filesystem.c           # Virtual filesystem
│   ├── interrupts/
│   │   └── interrupts.c           # Interrupt & exception handling
│   ├── syscall/
│   │   └── syscall.c              # System call dispatching
│   ├── loader/
│   │   └── loader.c               # Executable loaders (.exe, .trp, .elf)
│   ├── drivers/                   # Hardware drivers (future)
│   └── include/                   # Kernel headers
│       ├── types.h                # Type definitions
│       ├── config.h               # Configuration constants
│       ├── kernel.h               # Main kernel header
│       ├── mm.h                   # Memory management API
│       ├── process.h              # Process management API
│       ├── fs.h                   # Filesystem API
│       ├── interrupts.h           # Interrupt handling API
│       ├── syscall.h              # System call definitions
│       ├── loader.h               # Executable loader API
│       ├── io.h                   # I/O operations API
│       └── string.h               # String utilities API
```

### shell/ - Toriginal OS Shell
```
shell/
├── CMakeLists.txt                 # Shell build configuration
├── src/
│   ├── shell.cpp                  # Main shell implementation
│   └── main.cpp                   # Entry point
└── include/
    └── shell.h                    # Shell class definition
```

### docs/ - Documentation
```
docs/
├── ARCHITECTURE.md                # System architecture & design
├── KERNEL_API.md                  # Complete kernel API reference
├── TRP_FORMAT.md                  # TRP binary format specification
├── SHELL_USER_GUIDE.md            # Shell command reference
└── DEVELOPMENT.md                 # Developer guide & setup
```

### build/ - Build Artifacts (after compilation)
```
build/
├── freeNT                         # Compiled kernel binary
├── toriginal_shell                # Compiled shell executable
├── CMakeFiles/                    # CMake build system
└── Makefile                       # Generated Makefile
```

### install/ - Installation Directory
```
install/
├── boot/
│   └── freeNT                     # Kernel binary (installed)
├── bin/
│   └── toriginal_shell            # Shell executable (installed)
└── lib/                           # Libraries (for future use)
```

---

## 📚 Documentation Map

### For Users
- **README.md** - Overview and quick start
- **BUILD.txt** - Quick reference
- **docs/SHELL_USER_GUIDE.md** - How to use the shell

### For Developers
- **docs/ARCHITECTURE.md** - System design and internals
- **docs/DEVELOPMENT.md** - Development setup and guidelines
- **docs/KERNEL_API.md** - Kernel API reference

### For Technical Reference
- **docs/TRP_FORMAT.md** - Binary format specification
- **PROJECT_SUMMARY.md** - Complete project overview

---

## 🛠️ Build System Map

### Using Make (Recommended)
```bash
make all              # Build everything
make kernel           # Build kernel only
make shell            # Build shell only
make install          # Install to ./install
make run-shell        # Run shell for testing
make clean            # Remove build directory
make help             # Show all targets
```

### Using CMake Directly
```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make freeNT
make toriginal_shell
```

### Using Build Script
```bash
./build.sh all        # Build and install
./build.sh help       # Show options
```

---

## 🔍 How to Find Things

### Looking for...
- **How to build?** → See **README.md** or **BUILD.txt**
- **System architecture?** → See **docs/ARCHITECTURE.md**
- **Kernel API?** → See **docs/KERNEL_API.md**
- **Shell commands?** → See **docs/SHELL_USER_GUIDE.md**
- **Development setup?** → See **docs/DEVELOPMENT.md**
- **Binary format?** → See **docs/TRP_FORMAT.md**
- **Memory management code?** → See **freeNT/src/kernel/mm/memory.c**
- **Process scheduling code?** → See **freeNT/src/kernel/process/process.c**
- **Shell implementation?** → See **shell/src/shell.cpp**
- **System calls?** → See **freeNT/src/kernel/syscall/syscall.c**

---

## 📊 Project Statistics

| Category | Count |
|----------|-------|
| Kernel Source Files | 8 |
| Kernel Headers | 11 |
| Shell Source Files | 2 |
| Build Configuration Files | 3 |
| Documentation Files | 6 |
| Total Lines of Code | ~2700 |
| System Calls Implemented | 27 |
| Shell Commands | 17 |

---

## ✅ Component Status

| Component | Status | Location |
|-----------|--------|----------|
| Boot System | ✓ Complete | `freeNT/src/kernel/boot/` |
| Memory Management | ✓ Complete | `freeNT/src/kernel/mm/` |
| Process Management | ✓ Complete | `freeNT/src/kernel/process/` |
| Filesystem | ✓ Complete | `freeNT/src/kernel/fs/` |
| Interrupt Handling | ✓ Complete | `freeNT/src/kernel/interrupts/` |
| System Calls | ✓ Complete | `freeNT/src/kernel/syscall/` |
| Executable Loaders | ✓ Complete | `freeNT/src/kernel/loader/` |
| Shell | ✓ Complete | `shell/src/` |
| Documentation | ✓ Complete | `docs/` |
| Build System | ✓ Complete | Root level |

---

## 🎯 Key Features

### Kernel Features
- x86-64 long mode (64-bit)
- Memory management with paging
- Multi-process support
- Virtual filesystem
- Interrupt/exception handling
- System call interface
- Multiple binary format support

### Shell Features
- 17 built-in commands
- Directory navigation
- File operations
- System information
- Program execution
- Help and documentation

---

## 🔄 Common Workflows

### Build and Test
```bash
make all                    # Build
make run-shell             # Test shell
```

### Development
```bash
# Edit source files
nano freeNT/src/kernel/something.c
make kernel                # Rebuild
# Test changes
```

### Installation
```bash
make install               # Install to ./install
./install/bin/toriginal_shell  # Run installed shell
```

---

## 📖 Reading Order for New Users

1. **README.md** - Understand what this is
2. **BUILD.txt** - Quick reference
3. **docs/SHELL_USER_GUIDE.md** - Learn shell commands
4. **docs/ARCHITECTURE.md** - Understand system design
5. **docs/KERNEL_API.md** - Learn kernel programming
6. **docs/DEVELOPMENT.md** - Set up development environment

---

## 🚀 Next Steps

1. **Read**: Start with README.md
2. **Build**: Run `make all`
3. **Test**: Run `make run-shell`
4. **Explore**: Type `help` in shell
5. **Learn**: Read documentation in `docs/`
6. **Develop**: See `docs/DEVELOPMENT.md`

---

## 📞 Project Information

- **Name**: freeNT OS
- **Kernel**: freeNT (C99)
- **Shell**: Toriginal OS (C++17)
- **Architecture**: x86-64
- **Status**: Production Ready
- **Version**: 1.0.0
- **Release Date**: May 21, 2026

---

## 📋 File Naming Convention

### Kernel Files
- `*.c` - C source files
- `*.S` - Assembly files
- `*.h` - Header files
- `*.ld` - Linker scripts

### Shell Files
- `*.cpp` - C++ source files
- `*.h` - Header files

### Build Files
- `CMakeLists.txt` - CMake configuration
- `Makefile` - GNU Make file
- `*.sh` - Shell scripts

---

## 🔗 Quick Links to Key Files

- [Main README](README.md)
- [Kernel Entry Point](freeNT/src/kernel/kernel.c)
- [Shell Entry Point](shell/src/main.cpp)
- [Memory Manager](freeNT/src/kernel/mm/memory.c)
- [Process Scheduler](freeNT/src/kernel/process/process.c)
- [Filesystem](freeNT/src/kernel/fs/filesystem.c)
- [Interrupt Handler](freeNT/src/kernel/interrupts/interrupts.c)
- [Syscall Dispatcher](freeNT/src/kernel/syscall/syscall.c)

---

**Last Updated**: May 21, 2026
**Status**: Complete and Production-Ready

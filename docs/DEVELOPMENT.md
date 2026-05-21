# Development Guide

## Setting Up Development Environment

### Prerequisites
- Linux/Unix system or WSL2 on Windows
- GCC/Clang compiler suite
- CMake 3.10+
- GNU Make
- Git

### Installation (Ubuntu/Debian)

```bash
# Update package manager
sudo apt update

# Install build tools
sudo apt install -y build-essential cmake git

# Install cross-compiler (optional, for bare-metal kernel)
sudo apt install -y gcc-x86-64-linux-gnu

# Install GRUB tools (for bootable ISO)
sudo apt install -y grub-pc grub-common xorriso
```

## Cloning & Building

```bash
# Clone repository
git clone https://github.com/your-username/Toriganal-OS.git
cd Toriganal-OS

# Build the entire project
make all

# Install
make install

# Test the shell
make run-shell
```

## Source Code Organization

### Kernel Code (`freeNT/`)

**Entry Points:**
- `src/kernel/boot/boot.S` - Assembly bootloader
- `src/kernel/kernel.c` - Main kernel initialization

**Core Modules:**
```
src/kernel/mm/memory.c         → Memory management implementation
src/kernel/process/process.c   → Process management
src/kernel/fs/filesystem.c     → Virtual filesystem
src/kernel/interrupts/int.c    → Interrupt handling
src/kernel/syscall/syscall.c   → System call dispatching
src/kernel/loader/loader.c     → Executable loading
```

**Headers (Public API):**
```
src/kernel/include/kernel.h    → Main kernel header
src/kernel/include/types.h     → Type definitions
src/kernel/include/mm.h        → Memory management API
src/kernel/include/process.h   → Process management API
src/kernel/include/fs.h        → Filesystem API
src/kernel/include/syscall.h   → System call definitions
```

### Shell Code (`shell/`)

**Main Implementation:**
- `src/shell.cpp` - Shell command parsing and execution
- `src/main.cpp` - Entry point

**Headers:**
- `include/shell.h` - Shell class definition

## Build System

### CMake Structure

**Root:** `CMakeLists.txt`
- Defines project
- Includes subdirectories

**Kernel:** `freeNT/CMakeLists.txt`
- Kernel executable target
- Compiler flags
- ISO creation

**Shell:** `shell/CMakeLists.txt`
- Shell executable target
- C++17 standard
- Installation rules

### Makefile Targets

```bash
make all          # Build everything
make kernel       # Build kernel only
make shell        # Build shell only
make install      # Install to ./install
make clean        # Remove build directory
make distclean    # Complete cleanup
make help         # Show help
```

## Adding New Features

### Adding a Syscall

1. **Define syscall number** in `include/syscall.h`:
```c
#define SYS_MYNEW_SYSCALL 25
```

2. **Implement handler** in `syscall/syscall.c`:
```c
static uint64_t sys_mynew(uint64_t arg1, uint64_t arg2, ...) {
    /* Implementation */
    return 0;
}
```

3. **Register handler** in `syscall_init()`:
```c
syscall_handlers[SYS_MYNEW_SYSCALL] = sys_mynew;
```

### Adding a Shell Command

1. **Add to enum** in `include/shell.h`:
```cpp
enum class CommandType {
    // ...
    MY_NEW_COMMAND,
};
```

2. **Update parser** in `shell.cpp`:
```cpp
if (name == "mycommand") return CommandType::MY_NEW_COMMAND;
```

3. **Implement handler**:
```cpp
void ToriginalShell::cmd_mycommand(const vector<string> &args) {
    // Implementation
}
```

4. **Add to execute_command()**:
```cpp
case CommandType::MY_NEW_COMMAND:
    cmd_mycommand(cmd.args);
    break;
```

### Adding a Memory Zone

In `mm/memory.c`:
```c
/* Add zone to mem_zones array */
mem_zones[num_zones].start = zone_start;
mem_zones[num_zones].end = zone_end;
mem_zones[num_zones].free_pages = zone_pages;
num_zones++;
```

## Debugging

### Print Debug Info

Kernel debugging (console output):
```c
io_put_string("Debug message\n");
```

### Enabling Debug Flags

In `include/config.h`:
```c
#define DEBUG_MEMORY 1
#define DEBUG_PROCESS 1
#define DEBUG_FS 1
```

### Using GDB

```bash
# Build with debug symbols
cmake -DCMAKE_BUILD_TYPE=Debug ..

# Start debugger
gdb ./build/freeNT
```

## Code Style Guide

### C Code (Kernel)

```c
/* Function naming: verb_noun */
int process_create(const char *name);

/* Struct naming: typename_t */
typedef struct {
    uint64_t field;
} mystruct_t;

/* Macro naming: UPPER_CASE */
#define MAX_PROCESSES 1024

/* Local variables: lower_case */
int local_var = 0;

/* Constants: UPPER_CASE */
static const uint32_t TIMER_FREQ = 1000;
```

### C++ Code (Shell)

```cpp
// Class naming: PascalCase
class ToriginalShell { };

// Method naming: camelCase
void executeCommand();

// Member variables: snake_case_
std::string current_path_;

// Constants: kCamelCase
static const int kMaxProcesses = 1024;
```

## Testing

### Manual Testing

```bash
# Test shell commands
make run-shell

# Test filesystem operations
/sys/userpc/~ > mkdir testdir
/sys/userpc/~ > cd testdir
/sys/userpc/~/testdir > pwd
/sys/userpc/~/testdir >
```

### Kernel Testing

- Add test code to `kernel.c`
- Verify through console output
- Use serial port for logging

## Performance Optimization

### Memory
- Use stack for small allocations
- Minimize heap fragmentation
- Align structures to cache lines

### Processes
- Reduce context switch frequency
- Optimize scheduler
- Profile with built-in statistics

### Filesystem
- Cache frequently accessed inodes
- Optimize path resolution
- Use lazy loading for sections

## Submission Guidelines

When contributing:

1. **Code Quality**
   - Follow style guide
   - No compiler warnings
   - Clear comments for complex logic

2. **Documentation**
   - Update relevant .md files
   - Add function documentation
   - Include usage examples

3. **Testing**
   - Test your changes thoroughly
   - Verify no regressions
   - Document test cases

4. **Commits**
   - Clear commit messages
   - One feature per commit
   - Reference issues when applicable

## Common Issues & Solutions

| Issue | Cause | Solution |
|-------|-------|----------|
| Compilation error | Missing header | Check #include paths |
| Link error | Symbol not found | Verify function names |
| Runtime crash | Null pointer | Add pointer checks |
| Memory leak | Forgot kfree() | Track allocations |
| Slow performance | Inefficient algo | Profile & optimize |

## References

- x86-64 ABI Specification
- System V ABI x86-64 Supplement
- Intel x86-64 Manual (Vol. 1-3)
- CMake Documentation
- Linux Kernel Documentation

## Getting Help

1. Check existing documentation in `/docs`
2. Review source code comments
3. Look at similar implementations
4. Ask in project discussions

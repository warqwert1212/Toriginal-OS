# CMakeLists.txt for building a bare metal x86 kernel

cmake_minimum_required(VERSION 3.10)

# Set the target architecture and toolchain
set(CMAKE_SYSTEM_PROCESSOR x86)
set(CMAKE_SYSTEM_NAME BareMetal)
set(CMAKE_C_COMPILER i686-elf-gcc)
set(CMAKE_ASM_COMPILER nasm)

# Set the output format and flags
set(CMAKE_C_FLAGS "-ffreestanding -m32 -c")
set(CMAKE_ASM_FLAGS "-f elf")
set(CMAKE_EXE_LINKER_FLAGS "-T linker.ld -nostartfiles")

# Add source files
set(SOURCES
    main.cpp
    kernel_entry.asm
)

# Create the executable
add_executable(kernel ${SOURCES})
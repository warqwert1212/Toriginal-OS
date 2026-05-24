CMAKE_MINIMUM_REQUIRED(VERSION 3.10)
PROJECT(freeNT LANGUAGES C ASM)

SET(CMAKE_C_STANDARD 99)

# Prevent CMake from adding PIE flags
SET(CMAKE_POSITION_INDEPENDENT_CODE OFF)

# Kernel-specific compiler flags for 64-bit
ADD_COMPILE_OPTIONS(
    -mcmodel=large
    -ffreestanding
    -fno-stack-protector
    -fno-pie
    -fno-pic
    -fno-asynchronous-unwind-tables
    -Wall
    -Wextra
    -Wno-error=unused-but-set-variable
)

# Kernel executable sources - use new 64-bit bootloader
SET(KERNEL_SOURCES
    src/kernel/boot/boot64.S
    src/kernel/kernel.c
    src/kernel/string.c
    src/kernel/io.c
    src/kernel/graphics.c
    src/kernel/icons.c
    src/kernel/mm/memory.c
    src/kernel/process/process.c
    src/kernel/fs/filesystem.c
    src/kernel/interrupts/interrupts.c
    src/kernel/syscall/syscall.c
    src/kernel/loader/loader_enhanced.c
    src/kernel/boot/boot_detect.c
    src/kernel/shell/shell.c
)

ADD_EXECUTABLE(freeNT ${KERNEL_SOURCES})

TARGET_INCLUDE_DIRECTORIES(freeNT PRIVATE src/kernel/include)

# Set kernel-specific linker options
SET_TARGET_PROPERTIES(freeNT PROPERTIES 
    LINK_FLAGS "-T${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/boot/kernel64.ld -Wl,--build-id=none -Wl,-z,max-page-size=0x1000 -nostdlib -no-pie"
)

# Ensure no PIE on target
TARGET_COMPILE_OPTIONS(freeNT PRIVATE -fno-pie -fno-pic)
TARGET_LINK_OPTIONS(freeNT PRIVATE -no-pie)

# Create ISO image
ADD_CUSTOM_COMMAND(TARGET freeNT POST_BUILD
    COMMAND mkdir -p iso/boot/grub
    COMMAND cp freeNT iso/boot/freeNT
    COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/boot/grub.cfg iso/boot/grub/
    COMMAND grub-mkrescue -o freeNT.iso iso/ || true
)

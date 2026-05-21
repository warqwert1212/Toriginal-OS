CMAKE_MINIMUM_REQUIRED(VERSION 3.10)
PROJECT(freeNT LANGUAGES C ASM)

SET(CMAKE_C_STANDARD 99)

# Prevent CMake from adding PIE flags
SET(CMAKE_POSITION_INDEPENDENT_CODE OFF)
SET(CMAKE_C_FLAGS_INIT "-fno-pie -fno-pic")
SET(CMAKE_EXE_LINKER_FLAGS_INIT "-no-pie")

# Kernel-specific compiler flags
ADD_COMPILE_OPTIONS(-mcmodel=kernel -ffreestanding -fno-stack-protector -fno-pie -fno-pic -Wno-error=unused-but-set-variable)

# Kernel executable
SET(KERNEL_SOURCES
    src/kernel/boot/boot.S
    src/kernel/kernel.c
    src/kernel/string.c
    src/kernel/io.c
    src/kernel/mm/memory.c
    src/kernel/process/process.c
    src/kernel/fs/filesystem.c
    src/kernel/interrupts/interrupts.c
    src/kernel/syscall/syscall.c
    src/kernel/loader/loader.c
)

ADD_EXECUTABLE(freeNT ${KERNEL_SOURCES})

TARGET_INCLUDE_DIRECTORIES(freeNT PRIVATE src/kernel/include)

# Set kernel-specific linker options
SET_TARGET_PROPERTIES(freeNT PROPERTIES 
    LINK_FLAGS "-T${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/boot/kernel.ld -Wl,--build-id=none -nostdlib -no-pie -fno-pie"
)

# Ensure no PIE on target
TARGET_COMPILE_OPTIONS(freeNT PRIVATE -fno-pie -fno-pic)
TARGET_LINK_OPTIONS(freeNT PRIVATE -no-pie -fno-pie)

# Create ISO image
ADD_CUSTOM_COMMAND(TARGET freeNT POST_BUILD
    COMMAND mkdir -p iso/boot/grub
    COMMAND cp freeNT iso/boot/freeNT
    COMMAND cp src/kernel/boot/grub.cfg iso/boot/grub/
    COMMAND grub-mkrescue -o freeNT.iso iso/ 2>/dev/null || echo "Skipping ISO creation"
)

cmake_minimum_required(VERSION 3.16)
project(ToriginalOS C ASM)

# Add compilation parameters matching your cross-compiler definitions
set(CMAKE_C_FLAGS "-m32 -ffreestanding -O2 -Wall -Wextra")
set(CMAKE_ASM_FLAGS "-m32")
set(CMAKE_EXE_LINKER_FLAGS "-m32 -nostdlib -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/root/freeNT/src/kernel/boot/linker.ld")

# Instruction mapping to convert the separate binary artifact into an object block matching your linkage signatures
add_custom_command(
    OUTPUT toriginal_shell.o
    COMMAND ${CMAKE_OBJCOPY} -I binary -O elf32-i386 -B i386 path/to/toriginal_shell.bin toriginal_shell.o
    DEPENDS path/to/toriginal_shell.bin
    COMMENT "Baking user space shell component directly into an unmanaged object target..."
)

# Merge everything smoothly into the executable compilation block
add_executable(toriganal_os.bin 
    ${KERNEL_SOURCES} 
    ${INSTALLER_SOURCES}
    toriginal_shell.o
)

CMAKE_MINIMUM_REQUIRED(VERSION 3.10)
project(ToriginalOS C ASM)

get_filename_component(WORKSPACE_BUILD_DIR "${CMAKE_BINARY_DIR}/.." ABSOLUTE)

# =============================================================================
# 1. Installer subdirectory
# =============================================================================
add_subdirectory(src/installer)

# =============================================================================
# 2. Embedded shell payload
# =============================================================================
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/toriginal_shell.o" PROPERTIES GENERATED TRUE)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/toriginal_shell.o"
    COMMAND ${CMAKE_C_COMPILER} -ffreestanding -O2 -c
            "${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/shell/shell.c"
            -o "${CMAKE_CURRENT_BINARY_DIR}/shell_temp.o"
            -I"${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/shell"
            -I"${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/include"
            -I"${CMAKE_CURRENT_SOURCE_DIR}/src/kernel"
    COMMAND ${CMAKE_OBJCOPY} -I elf64-x86-64 -O binary
            "${CMAKE_CURRENT_BINARY_DIR}/shell_temp.o"
            "${CMAKE_CURRENT_BINARY_DIR}/toriginal_shell.bin"
    COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_CURRENT_BINARY_DIR}"
            ${CMAKE_OBJCOPY} -I binary -O elf64-x86-64 -B i386
            "toriginal_shell.bin" "toriginal_shell.o"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/shell/shell.c"
    COMMENT "Embedding shell payload..."
)
add_custom_target(shell_payload DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/toriginal_shell.o")

# =============================================================================
# 3. Kernel sources
#
# REMOVED from original:
#   src/kernel/drivers/main.c   — duplicate kernel_main()
#   src/kernel/drivers/idt.c    — duplicate IDT / init_idt()
#   src/kernel/heap.c           — duplicate kmalloc/kfree (memory.c owns heap now)
# =============================================================================
add_executable(freeNT
    # Boot
    src/kernel/boot/boot64.s
    src/kernel/boot/boot_detect.c

    # Core kernel
    src/kernel/kernel.c
    src/kernel/graphics.c
    src/kernel/icons.c
    src/kernel/io.c
    src/kernel/string.c
    src/kernel/stubs.c
    src/kernel/auth.c
    src/kernel/elf_loader.c

    # Memory  (heap.c REMOVED — memory.c owns kmalloc/kfree)
    src/kernel/mm/memory.c
    src/kernel/pmm.c
    src/kernel/vmm.c

    # Interrupts  (idt.c REMOVED — interrupts.c is the one true IDT)
    src/kernel/interrupts/interrupts.c

    # Syscalls
    src/kernel/syscall/syscall.c

    # Process & scheduler
    src/kernel/process/process.c
    src/kernel/scheduler.c

    # Filesystem
    src/kernel/fs/filesystem.c
    src/kernel/vfs.c
    src/kernel/fs_parsers.c

    # Loader  (trp_loader.c ADDED)
    src/kernel/loader/loader_enhanced.c
    src/kernel/loader/trp_loader.c

    # Shell
    src/kernel/shell/shell.c

    # Hardware drivers  (main.c REMOVED — duplicate kernel_main)
    src/kernel/drivers/serial.c
    src/kernel/drivers/vga.c
    src/kernel/drivers/video.c
    src/kernel/drivers/pci.c
    src/kernel/drivers/gdt.c
    src/kernel/drivers/drivers.c
    src/kernel/pit.c
    src/kernel/ahci.c

    # Keyboard (new)
    src/kernel/drivers/keyboard.c
    src/kernel/drivers/keyboard_wire.c
    src/kernel/drivers/keyboard_isr.s

    # Installer
    ${INSTALLER_SOURCES}

    # Embedded shell
    "${CMAKE_CURRENT_BINARY_DIR}/toriginal_shell.o"
)

add_dependencies(freeNT shell_payload)

# =============================================================================
# 4. Compiler flags — bare metal kernel
# =============================================================================
target_compile_options(freeNT PRIVATE
    -ffreestanding
    -O2
    -Wall
    -Wextra
    -mno-red-zone
    -mno-mmx
    -mno-sse
    -mno-sse2
    -fno-stack-protector
    -fno-pic
    -mcmodel=kernel
)

target_link_options(freeNT PRIVATE
    -nostdlib
    -no-pie
    -Wl,-z,noexecstack
    -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/boot/kernel64.ld
)

set_target_properties(freeNT PROPERTIES LINKER_LANGUAGE C)

target_include_directories(freeNT PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/kernel"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/boot"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/loader"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/installer"
)

# =============================================================================
# 5. ISO build target
# =============================================================================
find_program(GRUB_MKRESCUE_EXE grub-mkrescue)
if(GRUB_MKRESCUE_EXE)
    set(ISO_STAGING_DIR "${CMAKE_CURRENT_BINARY_DIR}/iso_staging")
    add_custom_target(iso
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${ISO_STAGING_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory   "${ISO_STAGING_DIR}/boot/grub"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/freeNT"
                                         "${ISO_STAGING_DIR}/boot/freeNT"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/boot/grub.cfg"
                                         "${ISO_STAGING_DIR}/boot/grub/grub.cfg"
        COMMAND ${GRUB_MKRESCUE_EXE} -o "${WORKSPACE_BUILD_DIR}/ToriginalOS.iso"
                                        "${ISO_STAGING_DIR}"
        DEPENDS freeNT
        COMMENT "Building ToriginalOS.iso..."
    )
else()
    message(WARNING "grub-mkrescue not found. Install grub-efi-amd64-bin.")
endif()

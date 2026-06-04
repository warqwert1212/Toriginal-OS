CMAKE_MINIMUM_REQUIRED(VERSION 3.10)

# Declare the project and explicitly enable C and Assembly (ASM)
# This ensures boot64.S is properly processed by the build toolchain
project(ToriginalOS C ASM)

# Calculate the definitive absolute path to the root build directory.
# This prevents subdirectories from losing track of the true build folder destination.
get_filename_component(WORKSPACE_BUILD_DIR "${CMAKE_BINARY_DIR}/.." ABSOLUTE)

# =============================================================================
# 1. Register the Dedicated Installer Component Directory
# =============================================================================
add_subdirectory(src/installer)

# =============================================================================
# 2. Setup Compile Rules for Embedded User Shell
# =============================================================================
# Explicitly tell CMake that this object file is dynamically generated during the build
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/toriginal_shell.o" PROPERTIES GENERATED TRUE)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/toriginal_shell.o"
    COMMAND ${CMAKE_C_COMPILER} -ffreestanding -O2 -c "${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/shell/shell.c" -o "${CMAKE_CURRENT_BINARY_DIR}/shell_temp.o" -I"${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/shell" -I"${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/include" -I"${CMAKE_CURRENT_SOURCE_DIR}/src/kernel"
    COMMAND ${CMAKE_OBJCOPY} -I elf64-x86-64 -O binary "${CMAKE_CURRENT_BINARY_DIR}/shell_temp.o" "${CMAKE_CURRENT_BINARY_DIR}/toriginal_shell.bin"
    COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_CURRENT_BINARY_DIR}" ${CMAKE_OBJCOPY} -I binary -O elf64-x86-64 -B i386 "toriginal_shell.bin" "toriginal_shell.o"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/shell/shell.c"
    COMMENT "Compiling, flattening, and embedding user-space shell payload..."
)

add_custom_target(shell_payload DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/toriginal_shell.o")

# =============================================================================
# 3. Compile the freeNT Kernel Target Image
# =============================================================================
add_executable(freeNT
    src/kernel/boot/boot64.S
    src/kernel/boot/boot_detect.c
    src/kernel/fs/filesystem.c
    src/kernel/graphics.c
    src/kernel/icons.c
    src/kernel/interrupts/interrupts.c
    src/kernel/io.c
    src/kernel/kernel.c
    src/kernel/loader/loader_enhanced.c
    src/kernel/mm/memory.c
    src/kernel/process/process.c
    src/kernel/string.c
    src/kernel/syscall/syscall.c
    ${INSTALLER_SOURCES}
    "${CMAKE_CURRENT_BINARY_DIR}/toriginal_shell.o"
)

# Force the main executable to wait until the binary object wrapper is completely finished
add_dependencies(freeNT shell_payload)

# =============================================================================
# 4. Apply Bare-Metal Cross-Compiler Flags
# =============================================================================
target_compile_options(freeNT PRIVATE -ffreestanding -O2 -Wall -Wextra)
target_link_options(freeNT PRIVATE -nostdlib -no-pie -Wl,-z,noexecstack)

set_target_properties(freeNT PROPERTIES LINKER_LANGUAGE C)

target_include_directories(freeNT PRIVATE 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/kernel"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/boot"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/installer"
)

# =============================================================================
# 5. Hybrid UEFI/BIOS Boot ISO Packaging Automation
# =============================================================================
find_program(GRUB_MKRESCUE_EXE grub-mkrescue)

if(GRUB_MKRESCUE_EXE)
    # Define a clean staging area inside the build folder to ensure exact structural layout
    set(ISO_STAGING_DIR "${CMAKE_CURRENT_BINARY_DIR}/iso_staging")

    add_custom_target(iso
        # Wipe any old staging data to guarantee a pristine image structure
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${ISO_STAGING_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ISO_STAGING_DIR}/boot/grub"
        
        # Copy the freshly compiled freeNT kernel binary directly into /boot
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/freeNT" "${ISO_STAGING_DIR}/boot/freeNT"
        
        # Copy the configuration file straight to the runtime path GRUB targets
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/boot/grub.cfg" "${ISO_STAGING_DIR}/boot/grub/grub.cfg"
        
        # Package the targeted structure explicitly avoiding broken or relative paths
        COMMAND ${GRUB_MKRESCUE_EXE} -o "${WORKSPACE_BUILD_DIR}/ToriginalOS.iso" "${ISO_STAGING_DIR}"
        DEPENDS freeNT
        COMMENT "Generating fixed hybrid BIOS + UEFI GRUB installation ISO from clean staging..."
    )
else()
    message(WARNING "grub-mkrescue wasn't found! Ensure grub-efi-amd64-bin is installed.")
endif()
cmake_minimum_required(VERSION 3.10)
project(os_kernel LANGUAGES C)

# Set 32-bit compilation flags
set(CMAKE_C_FLAGS "-m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs -Wall -Wextra -Werror -fno-pie")

# Custom command to compile assembly with NASM
add_custom_command(
    OUTPUT loader.o
    COMMAND nasm -f elf32 ${CMAKE_CURRENT_SOURCE_DIR}/loader.s -o loader.o
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/loader.s
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Compiling loader.s with NASM"
)

# Define sources
set(SOURCES
    kmain.c
)

# Create the executable (add loader.o as the first source for linking)
add_executable(kernel.elf ${CMAKE_CURRENT_BINARY_DIR}/loader.o ${SOURCES})

# Set linker options (use -Wl to pass flags to the linker when using the C compiler as a linker)
set_target_properties(kernel.elf PROPERTIES
    LINK_FLAGS "-m32 -static -no-pie -nostdlib -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/link.ld -Wl,-melf_i386"
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/link.ld"
)

# Custom command to create os.iso
add_custom_command(
    OUTPUT os.iso
    COMMAND cp kernel.elf ${CMAKE_CURRENT_SOURCE_DIR}/iso/boot/kernel.elf
    COMMAND genisoimage -R
            -b boot/grub/stage2_eltorito
            -no-emul-boot
            -boot-load-size 4
            -A os
            -input-charset utf8
            -quiet
            -boot-info-table
            -o os.iso
            ${CMAKE_CURRENT_SOURCE_DIR}/iso
    DEPENDS kernel.elf
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Creating ISO image"
)

# Define the iso target
add_custom_target(os_iso ALL DEPENDS os.iso)

# Define the run target
add_custom_target(run
    COMMAND qemu-system-i386 -cdrom os.iso -m 32 -boot d -monitor stdio
    DEPENDS os.iso
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running kernel with QEMU"
)

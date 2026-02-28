cmake_minimum_required(VERSION 3.10)
project(os_kernel LANGUAGES C)

# Set 32-bit compilation flags
set(CMAKE_C_FLAGS "-m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs -Wall -Wextra -Werror -fno-pie")

# Add include directories (OS headers + compiler headers)
include_directories(c_files/includes)
include_directories(compiler/include)
include_directories(compiler/src)

# --------------------------------------------------------------------------
# GUI mode option
#   cmake -B build -DGUI_MODE=ON   → builds with VESA framebuffer GUI
#   cmake -B build                 → default: text-mode shell
# --------------------------------------------------------------------------
option(GUI_MODE "Enable graphical desktop mode (VESA 1024x768x32)" ON)
if(GUI_MODE)
    add_definitions(-DGUI_MODE)
    message(STATUS "GUI mode: ON  (VESA 1024x768x32)")
else()
    message(STATUS "GUI mode: OFF (VGA text mode)")
endif()

# Custom command to compile loader.s with NASM
add_custom_command(
    OUTPUT loader.o
    COMMAND nasm -f elf32 ${CMAKE_CURRENT_SOURCE_DIR}/asm/loader.s -o loader.o
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/asm/loader.s
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Compiling loader.s with NASM"
)

# Custom command to compile stdio.s with NASM
add_custom_command(
    OUTPUT stdio.o
    COMMAND nasm -f elf32 ${CMAKE_CURRENT_SOURCE_DIR}/asm/stdio.s -o stdio.o
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/asm/stdio.s
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Compiling stdio.s with NASM"
)

# Custom command to compile gdt.s with NASM
add_custom_command(
    OUTPUT gdt.o
    COMMAND nasm -f elf32 ${CMAKE_CURRENT_SOURCE_DIR}/asm/gdt.s -o gdt.o
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/asm/gdt.s
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Compiling gdt.s with NASM"
)

# Custom command to compile idt.s with NASM
add_custom_command(
    OUTPUT idt.o
    COMMAND nasm -f elf32 ${CMAKE_CURRENT_SOURCE_DIR}/asm/idt.s -o idt.o
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/asm/idt.s
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Compiling idt.s with NASM"
)

# Custom command to compile isr.s with NASM
add_custom_command(
    OUTPUT isr.o
    COMMAND nasm -f elf32 ${CMAKE_CURRENT_SOURCE_DIR}/asm/isr.s -o isr.o
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/asm/isr.s
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Compiling isr.s with NASM"
)

# Custom command to compile program.s as flat binary with NASM
add_custom_command(
    OUTPUT program
    COMMAND nasm -f bin ${CMAKE_CURRENT_SOURCE_DIR}/asm/program.s -o program
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/asm/program.s
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Compiling program.s as flat binary with NASM"
)

# User-mode test programs (flat binaries for future GRUB module loading)
add_custom_command(
    OUTPUT prog_a
    COMMAND nasm -f bin ${CMAKE_CURRENT_SOURCE_DIR}/asm/userprogs/prog_a.s -o prog_a
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/asm/userprogs/prog_a.s
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Compiling user program A (flat binary)"
)
add_custom_command(
    OUTPUT prog_b
    COMMAND nasm -f bin ${CMAKE_CURRENT_SOURCE_DIR}/asm/userprogs/prog_b.s -o prog_b
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/asm/userprogs/prog_b.s
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Compiling user program B (flat binary)"
)
add_custom_target(user_progs ALL DEPENDS prog_a prog_b)

# Copy program to iso/modules
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/iso/modules/program
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/program ${CMAKE_CURRENT_SOURCE_DIR}/iso/modules/program
    DEPENDS program
    COMMENT "Copying program module to iso/modules"
)

add_custom_target(program_module ALL DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/iso/modules/program)

# Define sources
file(GLOB_RECURSE SOURCES "c_files/src/*.c")

# Compiler (rosc) sources – built directly into the kernel for Phase 1.
# No file system is available yet; the test source is hardcoded in rosc.c.
file(GLOB COMPILER_SOURCES "compiler/src/*.c")

# Create the executable (add NASM objects as sources for linking)
add_executable(kernel.elf
    ${CMAKE_CURRENT_BINARY_DIR}/loader.o
    ${CMAKE_CURRENT_BINARY_DIR}/stdio.o
    ${CMAKE_CURRENT_BINARY_DIR}/gdt.o
    ${CMAKE_CURRENT_BINARY_DIR}/idt.o
    ${CMAKE_CURRENT_BINARY_DIR}/isr.o
    ${SOURCES}
    ${COMPILER_SOURCES}
)

# Set linker options (use -Wl to pass flags to the linker when using the C compiler as a linker)
set_target_properties(kernel.elf PROPERTIES
    LINK_FLAGS "-m32 -static -no-pie -nostdlib -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/linker/link.ld -Wl,-melf_i386"
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/linker/link.ld"
)

# Custom command to create os.iso using GRUB 2 (grub-mkrescue).
# GRUB 2 honours the Multiboot VIDEO_MODE flag (bit 2) and populates
# multiboot_info_t::framebuffer_* so fb_init() gets a valid VESA framebuffer.
# The ISO source tree (iso/) must contain iso/boot/grub/grub.cfg.
add_custom_command(
    OUTPUT os.iso
    COMMAND cp kernel.elf ${CMAKE_CURRENT_SOURCE_DIR}/iso/boot/kernel.elf
    COMMAND grub-mkrescue -o os.iso ${CMAKE_CURRENT_SOURCE_DIR}/iso
            --quiet
    DEPENDS kernel.elf ${CMAKE_CURRENT_SOURCE_DIR}/iso/modules/program
            ${CMAKE_CURRENT_SOURCE_DIR}/iso/boot/grub/grub.cfg
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Creating ISO image (GRUB 2)"
)

# Define the iso target
add_custom_target(os_iso ALL DEPENDS os.iso)

# --------------------------------------------------------------------------
# Hard-disk image for the FAT32 filesystem.
# Created once (64 MB raw); the kernel's fs_init() will format it on first
# boot and reuse the existing filesystem on subsequent boots.
# --------------------------------------------------------------------------
set(DISK_IMG "${CMAKE_BINARY_DIR}/disk.img")
add_custom_command(
    OUTPUT ${DISK_IMG}
    COMMAND ${CMAKE_COMMAND} -E env dd if=/dev/zero of=${DISK_IMG} bs=1M count=64 status=none
    COMMENT "Creating 64 MB raw disk image"
)
add_custom_target(disk_img DEPENDS ${DISK_IMG})

# --------------------------------------------------------------------------
# run  – standard launch target.
#
#   -vga std      exposes a VESA BIOS so GRUB can set a linear framebuffer;
#                 required for GUI mode (gui_init / 'mode gui' command).
#   -m 64         64 MB RAM needed for the ~3 MB VESA back-buffer.
#   -serial ...   serial output to com1.out for kernel log messages.
#
# Usage:
#   ninja run            (inside the build directory)
#   cmake --build build --target run
# --------------------------------------------------------------------------
add_custom_target(run
    COMMAND qemu-system-i386
            -cdrom os.iso
            -m 64
            -boot d
            -hda ${DISK_IMG}
            -vga std
            -enable-kvm
            -serial file:com1.out
    DEPENDS os.iso disk_img
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Launching RabinOS in QEMU"
)

# --------------------------------------------------------------------------
# run_nerd  – text-mode only (no VESA), useful for quick shell testing.
# --------------------------------------------------------------------------
add_custom_target(run_nerd
    COMMAND qemu-system-i386
            -cdrom os.iso
            -m 32
            -boot d
            -hda ${DISK_IMG}
            -serial file:com1.out
    DEPENDS os.iso disk_img
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Launching RabinOS in QEMU (text/nerd mode, no VESA)"
)

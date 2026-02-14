# My RandomOS Project

This is a simple experimental operating system kernel written in C and Assembly. It is currently in the very early stages of development.

## Project Structure

- `asm/`: Assembly source files (bootloader stub, etc.)
- `c_files/`: C source files (kernel main, drivers, std library implementations)
  - `src/`: Source code (`.c`)
  - `includes/`: Header files (`.h`)
- `iso/`: ISO directory structure including GRUB configuration
- `linker/`: Linker script
- `build/`: Build artifacts

## Prerequisites

To build and run this project, you need the following tools installed:

- GCC (`gcc` with `-m32` support) or a Cross-compiler
- NASM (Netwide Assembler)
- CMake
- Make or Ninja
- `genisoimage` (for creating the ISO)
- Bochs or QEMU (for running the OS)

## Building

This project uses CMake. To build the kernel and the ISO image:

1. Create a build directory (if you haven't already):
   ```bash
   mkdir -p build
   cd build
   ```

2. Configure the project:
   ```bash
   cmake .. -G Ninja # Or just cmake .. if using Make
   ```

3. Build:
   ```bash
   ninja # Or make
   ```
   This will compile the assembly and C files, link them into `kernel.elf`, and generate `os.iso`.

## Running

You can run the generated ISO image using an emulator.

### Using Bochs
If you have Bochs installed and configured with `bochsrc.txt`:
```bash
bochs -f bochsrc.txt
```

### Using QEMU
```bash
qemu-system-i386 -cdrom build/os.iso
```

## Status

Currently implementation includes:
- Bootstrapping (Assembly loader)
- Basic Kernel Main
- Serial logging
- Standard I/O (framebuffer text output)

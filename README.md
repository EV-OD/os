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
- `docs/`: Documentation and design notes
- `cheatsheets/`: Reference materials for x86 architecture, assembly syntax, etc.

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

From the build directory you can also use the CMake target to boot directly in QEMU:
```bash
ninja run
```
This starts `qemu-system-i386` with `os.iso` and captures serial output to `com1.out`.

### Using Bochs
If you have Bochs installed and configured with `bochsrc.txt`:
```bash
bochs -f bochsrc.txt
```

### Using QEMU
```bash
qemu-system-i386 -cdrom build/os.iso
```

## Current Progress

- Protected-mode bring-up with a 3-entry GDT (null, kernel code, kernel data) and an IDT installed via `lidt`.
- Exception and IRQ plumbing: stubs for CPU exceptions (0–31), PIC remapped to 0x20/0x28, default dispatcher with serial logging and end-of-interrupt handling, timer IRQ silenced by a stub handler.
- Keyboard input: PS/2 set-1 interrupt handler with a ring buffer; hooks into the stdio helpers so `getchar`, `readline`, and `scanf` can consume keypresses.
- Text I/O and logging: framebuffer text console with simple printf/scanf-style routines plus COM1 serial output (initialized at 9600 baud) for debugging.
- Kernel demo path exercises the I/O stack by prompting for a word, line, number, and single keypress at boot.

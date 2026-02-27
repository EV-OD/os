# Multiboot Module Loading

This document describes how the kernel loads and executes binary modules passed by the GRUB bootloader via the Multiboot specification. The implementation lives in `c_files/src/module.c` with the public API in `c_files/includes/module.h`.

## Multiboot Protocol

GRUB follows the Multiboot specification. When the kernel boots:
- `eax` = `0x2BADB002` (Multiboot magic number) — confirms a compliant bootloader.
- `ebx` = physical address of the `multiboot_info_t` structure — contains memory maps, module locations, and other boot information.

The `multiboot.h` header defines all the structures and flag constants from the specification.

## Module Loading Flow

`module_run(eax, ebx)` is the entry point:

1. **Validate magic**: checks that `eax == MULTIBOOT_BOOTLOADER_MAGIC`. If not, the kernel was not booted by a Multiboot-compliant loader.

2. **Check module info**: the `flags` field of `multiboot_info_t` must have `MULTIBOOT_INFO_MODS` set, confirming module information is available.

3. **Expect exactly one module**: the current implementation only supports a single GRUB module. `mods_count` must be 1.

4. **Resolve virtual address**: GRUB reports module addresses as physical addresses. Since the kernel runs in the higher half, `KERNEL_VIRTUAL_BASE` (0xC0000000) is added to both `mods_addr` and `mod_start` to obtain usable virtual pointers.

5. **Execute**: the module's start address is cast to a function pointer (`void (*)(void)`) and called directly.

## GRUB Configuration

In `iso/boot/grub/menu.lst`, the module is declared alongside the kernel:

```
kernel /boot/kernel.elf
module /modules/program
```

The `program` binary is assembled from `asm/program.s` as a flat binary (`nasm -f bin`) and copied to `iso/modules/program` by CMake.

## The Sample Module (program.s)

The included module writes "Hello from userland!" to the VGA framebuffer:

- Uses position-independent code (call/pop trick) since the load address is unknown at assemble time.
- Writes directly to the VGA text buffer at `0xC00B8000` (higher-half mapped address), row 5.
- Uses light green on black (`0x0A`) as the text attribute.
- Halts after printing.

## Multiboot Info Structure

Key fields of `multiboot_info_t` used by the module loader:

| Field | Type | Description |
|-------|------|-------------|
| `flags` | u32 | Bitmask indicating which fields are valid |
| `mods_count` | u32 | Number of boot modules loaded |
| `mods_addr` | u32 | Physical address of the module list |

Each module entry (`multiboot_module_t`) contains:

| Field | Type | Description |
|-------|------|-------------|
| `mod_start` | u32 | Physical start address of the module |
| `mod_end` | u32 | Physical end address of the module |
| `cmdline` | u32 | Physical address of the module command line string |

## Error Codes

| Return | Meaning |
|--------|---------|
| 0 | Success |
| -1 | Not booted by a Multiboot-compliant bootloader |
| -2 | No module information available |
| -3 | Unexpected module count (not exactly 1) |

## Future Work

- Support multiple modules (e.g. an init process and a file system image).
- Validate module integrity before execution (checksums, ELF headers).
- Run modules in ring 3 user mode instead of kernel mode.

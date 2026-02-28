# Shell & Boot System

## Overview

RabinOS includes a built-in kernel shell (**rosh** – Rabin OS Shell) with a
text-based boot animation, POSIX-like filesystem commands, and support for
external `.rox` executables.

## Boot Flow

```
kernel_init()          Hardware setup (GDT, IDT, PFA, kheap, PIT, ATA, FAT32)
  │
  ├── Heap smoke test  (logged to serial)
  ├── Self-tests       (PFA + kheap, serial only)
  ├── FS smoke test    (serial only)
  │
  ├── shell_init()     Creates /bin, /etc, /home, /tmp, /var, /var/log
  │                    Writes /etc/hostname, /etc/motd
  │
  ├── boot_animation() ASCII art logo + progress bar
  │
  └── shell_run()      Interactive command loop (never returns)
```

## OS Modes

| Mode | Description | Status |
|------|-------------|--------|
| `nerd` | Shell-only, text mode | **Implemented** |
| `gui`  | Graphical mode | Planned (future) |

Use the `mode` command to check or set the mode.

## Shell Commands

| Command | Usage | Description |
|---------|-------|-------------|
| `help` | `help` | Show all available commands |
| `ls` | `ls [-a] [path]` | List directory contents |
| `cd` | `cd [path]` | Change working directory |
| `cat` | `cat <file>` | Display file contents |
| `echo` | `echo <text...>` | Print text to screen |
| `clear` | `clear` | Clear the screen |
| `mkdir` | `mkdir <dir>` | Create a directory |
| `touch` | `touch <file>` | Create an empty file |
| `rm` | `rm <file>` | Remove a file |
| `pwd` | `pwd` | Print working directory |
| `mode` | `mode [nerd\|gui]` | Show/set OS mode |
| `stat` | `stat <path>` | Show file/directory info |
| `write` | `write <file> <text...>` | Write text to a file |
| `uname` | `uname` | Print system information |
| `whoami` | `whoami` | Print current user |

## Command Resolution

When a command is entered, the shell resolves it in this order:

1. **External**: Check `/bin/<command>.rox` — if it exists, load and execute it
2. **Built-in**: Search the built-in command table
3. **Not found**: Print error message

## .rox Executable Format

The **R**abin **O**S e**X**ecutable format is a simple flat binary with a 32-byte header:

```
Offset  Size  Field
  0      4    magic         (0x524F5821 = "ROX!")
  4      4    version       (1)
  8      4    entry_offset  (byte offset of entry point in code section)
 12      4    code_size     (size of code+data section)
 16      4    flags         (reserved, 0)
 20     12    name          (null-terminated program name)
```

Code section follows immediately after the header at byte offset 32.

### Creating a .rox file

Phase 1: Executables are kernel-mode C functions with signature:
```c
void entry(int argc, char **argv)
```

## Directory Structure

Created at boot time:

```
/
├── bin/          Executable programs (.rox files)
├── etc/          Configuration files
│   ├── hostname  Machine hostname
│   └── motd      Message of the day
├── home/         User home directories
├── tmp/          Temporary files
├── var/
│   └── log/      Log files
├── testdir/      (from FS smoke test)
└── hello.txt     (from FS smoke test)
```

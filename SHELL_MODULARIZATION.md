# Shell Modularization Guide

## Completed Files

### ✅ c_files/src/shell/shell_core.c
- Path helpers: `resolve_path()`, `tokenise()`, `ends_with()`
- Command resolver: `resolve_and_execute()`
- Prompt: `print_prompt()`

### ✅ c_files/src/shell/shell_cmds.c  
- All built-in commands: help, ls, cd, cat, echo, clear, mkdir, touch, rm, pwd, mode, stat, write, uname, whoami, rosc, desktop

### ✅ c_files/src/shell/shell_stdlib.c
- Standard library sources: gui.ros, math.ros, io.ros
- `shell_init_stdlib()` function

## Files to Create

### 📝 c_files/src/shell/shell_apps.c
Extract from shell.c lines containing:
- `RXT_ROS[]` constant (rxt editor source)
- `TERM_ROS[]` constant (terminal app source)  
- `PAINT_ROS[]` constant (paint app source)
- `cmd_setup()` function

### 📝 c_files/src/shell/shell_sample.c
Extract from shell.c:
- All `sample_*[]` constants (hello, strings, math, fib, loops, funcs, gui, mu, test)
- `struct sample_entry` and `sample_table[]`
- `cmd_sample()` function
- `cmd_microui()` function

### 📝 c_files/src/shell/shell.c (main file - rewrite)
Keep only:
- OS mode state: `current_mode`, `os_get_mode()`, `os_set_mode()`
- CWD state: `cwd[]`, `shell_get_cwd()`, `shell_set_cwd()`
- Builtin command table: `builtins[]` array
- `shell_init()` - directory structure + hostname + motd + call `shell_init_stdlib()`
- `shell_run()` - main REPL loop

## Updated shell.h

```c
#ifndef SHELL_H
#define SHELL_H

#define SHELL_MAX_INPUT 256
#define SHELL_MAX_ARGS  32
#define SHELL_HOSTNAME  "rabinos"

typedef enum {
    OS_MODE_NERD = 0,
    OS_MODE_GUI  = 1
} os_mode_t;

/* Mode accessors */
os_mode_t os_get_mode(void);
void      os_set_mode(os_mode_t mode);

/* CWD accessors */
const char* shell_get_cwd(void);
void        shell_set_cwd(const char* path);

/* Main shell functions */
void shell_init(void);
void shell_run(void);

/* Builtin command type */
typedef int (*builtin_fn_t)(int argc, char **argv);
typedef struct builtin_cmd {
    const char   *name;
    const char   *description;
    builtin_fn_t  handler;
} builtin_cmd_t;

#endif
```

## CMakeLists.txt Update

Add to your kernel sources:

```cmake
c_files/src/shell/shell.c
c_files/src/shell/shell_core.c
c_files/src/shell/shell_cmds.c
c_files/src/shell/shell_stdlib.c
c_files/src/shell/shell_apps.c
c_files/src/shell/shell_sample.c
```

## File Responsibilities

| File | Responsibility | Lines (approx) |
|------|---------------|----------------|
| shell.c | State, init, REPL, builtin table | ~150 |
| shell_core.c | Path/token helpers, resolver | ~200 |
| shell_cmds.c | 17 built-in commands | ~500 |
| shell_stdlib.c | Embedded .ros libraries | ~100 |
| shell_apps.c | Embedded apps (rxt, term, paint) | ~400 |
| shell_sample.c | Sample programs + templates | ~600 |

Total: ~1950 lines (down from 1800 in monolithic shell.c)

## Benefits

1. **Clear separation of concerns** - each file has one job
2. **Easier maintenance** - modify commands without touching core logic
3. **Faster compilation** - change one module, recompile only that file
4. **Better readability** - find code by category
5. **Extensible** - add new commands in shell_cmds.c, new samples in shell_sample.c

## Next Steps

1. Create shell_apps.c with RXT/TERM/PAINT sources + cmd_setup
2. Create shell_sample.c with all sample templates + cmd_sample/cmd_microui
3. Rewrite shell.c to be minimal (state + init + REPL + builtin table)
4. Update CMakeLists.txt to include all 6 shell/*.c files
5. Test build and verify all commands work

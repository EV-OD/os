#ifndef SHELL_H
#define SHELL_H

/* =========================================================================
 * shell.h – Built-in kernel shell (rosh – Rabin OS Shell)
 *
 * Provides a command-line interface inside the kernel.  Commands are either
 * built-in (ls, cd, cat, echo, clear, mkdir, touch, rm, pwd, help, mode)
 * or external .rox executables discovered in /bin.
 *
 * Command resolution order:
 *   1. Search /bin for <command>.rox  → execute via .rox loader
 *   2. Fall back to the built-in command table
 *   3. Print "command not found"
 *
 * OS modes:
 *   NERD  – text-only shell (current)
 *   GUI   – graphical mode  (future placeholder)
 * ========================================================================= */

/* -------------------------------------------------------------------------
 * OS boot modes
 * ------------------------------------------------------------------------- */
typedef enum os_mode {
    OS_MODE_NERD  = 0,   /**< Shell-only (text mode)       */
    OS_MODE_GUI   = 1    /**< Graphical mode (future)       */
} os_mode_t;

/* -------------------------------------------------------------------------
 * Shell configuration
 * ------------------------------------------------------------------------- */
#define SHELL_MAX_INPUT     256   /**< Maximum command-line length         */
#define SHELL_MAX_ARGS      16    /**< Maximum argument count per command  */
#define SHELL_PROMPT        "rosh> "
#define SHELL_HOSTNAME      "rabinos"

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/**
 * shell_init – create the OS directory structure (/bin, /etc, /home, /tmp).
 * Idempotent: safe to call on every boot.
 */
void shell_init(void);

/**
 * shell_run – enter the interactive shell loop (never returns).
 * This is the main entry point for nerd mode.
 */
void shell_run(void);

/**
 * os_get_mode – return the current OS boot mode.
 */
os_mode_t os_get_mode(void);

/**
 * os_set_mode – set the OS boot mode.
 */
void os_set_mode(os_mode_t mode);

#endif /* SHELL_H */

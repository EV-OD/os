#ifndef ROSC_H
#define ROSC_H

/* -----------------------------------------------------------------------
 * rosc.h  --  Public entry point for the RandomOS Compiler (rosc).
 *
 * Call rosc_run() from kmain to launch the Phase 1 compiler shell.
 * The shell compiles a hardcoded test program (no file system needed)
 * and displays tokens, AST, generated hex, and runtime results.
 * --------------------------------------------------------------------- */

void rosc_run(void);

#endif /* ROSC_H */

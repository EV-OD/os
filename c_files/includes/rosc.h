#ifndef ROSC_H
#define ROSC_H

/* -----------------------------------------------------------------------
 * rosc.h  --  Public entry point for the RandomOS Compiler (rosc).
 *
 * rosc_compile() reads a .ros source file from the VFS, runs the full
 * compilation pipeline (lex → parse → codegen), and writes a .rox
 * executable to the VFS.
 *
 * rosc_run() is the interactive demo mode (legacy Phase 1).
 * --------------------------------------------------------------------- */

/**
 * rosc_compile – compile a .ros file into a .rox executable.
 *
 * @param src_path   VFS path to the .ros source file (e.g. "/home/test.ros").
 * @param out_path   VFS path for the .rox output file (e.g. "/home/test.rox").
 *                   If NULL, the output path is derived from src_path by
 *                   replacing the .ros extension with .rox.
 * @return           0 on success, negative on error.
 */
int rosc_compile(const char *src_path, const char *out_path);

/**
 * rosc_run – interactive demo: compile and display the hardcoded test program.
 * (Legacy Phase 1 demo – press-any-key between stages.)
 */
void rosc_run(void);

#endif /* ROSC_H */

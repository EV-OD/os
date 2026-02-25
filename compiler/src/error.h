#ifndef COMPILER_ERROR_H
#define COMPILER_ERROR_H

/* -----------------------------------------------------------------------
 * error.h  --  Diagnostic reporting for the rosc compiler.
 *
 * Errors are printed directly to the OS framebuffer via puts().
 * Format:  error[<category>]: <message>  (line <L>, col <C>)
 * --------------------------------------------------------------------- */

#include "common.h"

/* Reset the error counter (call before each compilation unit). */
void error_init(void);

/* Emit a formatted error message to the display. */
void error_report(const char *category, const char *msg, int line, int col);

/* Return how many errors have been reported since the last error_init(). */
int  error_count(void);

#endif /* COMPILER_ERROR_H */

#ifndef SHELL_CORE_H
#define SHELL_CORE_H

#include "shell.h"

/* Accessors for shared state */
const char* shell_get_cwd(void);
void        shell_set_cwd(const char* path);

/* Path helpers */
void resolve_path(const char *input, char *out, unsigned int out_size);
int  tokenise(char *input, char **argv, int max_args);
int  ends_with(const char *str, const char *suffix);

/* Shell prompt and execution */
void print_prompt(void);
int  resolve_and_execute(int argc, char **argv);

#endif /* SHELL_CORE_H */

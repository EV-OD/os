#ifndef SHELL_CMDS_H
#define SHELL_CMDS_H

/* Built-in command handlers */
int cmd_help(int argc, char **argv);
int cmd_ls(int argc, char **argv);
int cmd_cd(int argc, char **argv);
int cmd_cat(int argc, char **argv);
int cmd_echo(int argc, char **argv);
int cmd_clear(int argc, char **argv);
int cmd_mkdir(int argc, char **argv);
int cmd_touch(int argc, char **argv);
int cmd_rm(int argc, char **argv);
int cmd_pwd(int argc, char **argv);
int cmd_mode(int argc, char **argv);
int cmd_stat(int argc, char **argv);
int cmd_write(int argc, char **argv);
int cmd_uname(int argc, char **argv);
int cmd_whoami(int argc, char **argv);
int cmd_rosc(int argc, char **argv);
int cmd_desktop(int argc, char **argv);

#endif /* SHELL_CMDS_H */

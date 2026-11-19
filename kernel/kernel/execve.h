#ifndef EXECVE_H
#define EXECVE_H

#include "../include/types.h"

#define EXEC_MAX_ARGS   16
#define EXEC_CMDLINE_MAX 256

/* Run "<file> [args...]" from the current directory. Returns the app's exit
 * status, or -1 if it could not be started. Frees the app image afterwards. */
int  sys_exec(const char* cmdline);

/* Terminate the running app and return to the caller of sys_exec. */
void exec_terminate(int status);

bool     exec_app_running();
uint32_t exec_app_base();

#endif

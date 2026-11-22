#ifndef EXECVE_H
#define EXECVE_H

#include "../include/types.h"

#define EXEC_MAX_ARGS   16
#define EXEC_CMDLINE_MAX 256

/* Load "<file> [args...]" (looked up in the current directory) into a new task
 * with its own private RAM and address space, and make it runnable. Returns the
 * new pid, or a negative number (an error message has already been printed).
 *   detach = true : nobody will task_wait() for it; its slot is recycled on exit
 *   detach = false: the caller must task_wait() for it to collect the status */
int  exec_spawn(const char* cmdline, bool detach);

/* Shell `exec`: spawn in the foreground, give it the keyboard and wait for it
 * (Ctrl+C kills it). Returns the exit status, or -1 if it could not start. */
int  sys_exec(const char* cmdline);

#endif

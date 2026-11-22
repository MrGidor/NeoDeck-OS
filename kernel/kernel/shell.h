#ifndef SHELL_H
#define SHELL_H

/* Feed one typed character to the shell (line editing + command dispatch). */
void shell_handle_char(char c);

/* Kernel-thread entry point of the shell: prints the banner, then reads the
 * keyboard forever (blocking, so other tasks keep running). */
void shell_main();

#endif

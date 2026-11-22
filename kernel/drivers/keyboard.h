#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "../include/types.h"

#define KEYBOARD_BUFFER_SIZE 256   /* must be a power of two */

void keyboard_init();               /* hook IRQ1 and unmask it */

/* Ring buffer filled by the IRQ1 handler, drained by the main loop / syscalls. */
bool keyboard_has_chars();
char keyboard_pop_char();           /* non-blocking, returns 0 if empty */
char keyboard_get_char();           /* blocks (other tasks keep running) until a char is available */

/* Ctrl+C is not queued as a character: it only raises this flag, which `exec`
 * uses to abort the foreground app. */
bool keyboard_ctrl_c_pending();
void keyboard_ctrl_c_clear();

#endif

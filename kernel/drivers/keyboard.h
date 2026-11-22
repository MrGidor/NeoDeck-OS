#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "../include/types.h"

#define KEYBOARD_BUFFER_SIZE 256

void keyboard_init();

bool keyboard_has_chars();
char keyboard_pop_char();
char keyboard_get_char();

/* Ctrl+C is not queued as a character: it only raises this flag, which `exec`
 * uses to abort the foreground app. */
bool keyboard_ctrl_c_pending();
void keyboard_ctrl_c_clear();

#endif

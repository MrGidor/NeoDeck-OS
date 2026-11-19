#ifndef STRING_H
#define STRING_H

int kstrcmp(const char* s1, const char* s2);
int kstrchr(const char* str, char c);
int kstrncmp(const char* s1, const char* s2, int n);
void kstrncpy(char* dest, const char* src, int n);

/* Split `line` in place on spaces (double quotes group words) into argv[].
 * Returns argc; argv[argc] is NULL. At most max_args tokens are produced. */
int ktokenize(char* line, char** argv, int max_args);
/* Parse an unsigned decimal; returns 0 on empty / invalid input. */
unsigned int katou(const char* s);

#endif
#ifndef STRING_H
#define STRING_H

int kstrcmp(const char* s1, const char* s2);
int kstrchr(const char* str, char c);
int kstrncmp(const char* s1, const char* s2, int n);
void kstrncpy(char* dest, const char* src, int n);

#endif
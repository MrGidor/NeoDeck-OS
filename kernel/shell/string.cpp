typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

#include "string.h"

int kstrcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int kstrchr(const char* str, char c) {
    int i = 0;
    while (str[i] != '\0') {
        if (str[i] == c) return i;
        i++;
    }
    return -1;
}

int kstrncmp(const char* s1, const char* s2, int n) {
    for (int i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return s1[i] - s2[i];
        if (s1[i] == '\0') break;
    }
    return 0;
}

void kstrncpy(char* dest, const char* src, int n) {
    for (int i = 0; i < n; i++) {
        dest[i] = src[i];
        if (src[i] == '\0') break;
    }
}
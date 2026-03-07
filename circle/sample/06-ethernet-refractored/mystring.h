#ifndef _mystring_h
#define _mystring_h

#include <circle/string.h>

// Custom memory functions to avoid include conflicts
void *my_memcpy(void *dest, const void *src, unsigned int n);
void *my_memset(void *s, int c, unsigned int n);
int my_memcmp(const void *s1, const void *s2, unsigned int n);

#endif

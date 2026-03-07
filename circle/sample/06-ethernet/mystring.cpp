#include "mystring.h"

void *my_memcpy(void *dest, const void *src, unsigned int n)
{
	u8 *d = (u8 *)dest;
	const u8 *s = (const u8 *)src;
	for (unsigned int i = 0; i < n; i++)
		d[i] = s[i];
	return dest;
}

void *my_memset(void *s, int c, unsigned int n)
{
	u8 *p = (u8 *)s;
	for (unsigned int i = 0; i < n; i++)
		p[i] = (u8)c;
	return s;
}

int my_memcmp(const void *s1, const void *s2, unsigned int n)
{
	const u8 *a = (const u8 *)s1;
	const u8 *b = (const u8 *)s2;
	for (unsigned int i = 0; i < n; i++)
	{
		if (a[i] != b[i])
			return (int)a[i] - (int)b[i];
	}
	return 0;
}


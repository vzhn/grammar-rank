#ifndef UTIL_H
#define UTIL_H
#include <stddef.h>

void die(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t size);
void *xrealloc(void *p, size_t n);
char *xstrndup(const char *s, size_t n);

#endif

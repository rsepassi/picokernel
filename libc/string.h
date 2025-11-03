#pragma once

#include <stddef.h>

int memcmp(const void *lhs, const void *rhs, size_t count);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *dest, int ch, size_t count);
void *memchr(const void *ptr, int ch, size_t count);

size_t strlen(const char *str);
size_t strnlen(const char *str, size_t maxlen);
char *strchr(const char *str, int ch);
char *strrchr(const char *str, int ch);
int strcmp(const char *s1, const char *s2);
unsigned long strtoul(const char *str, char **str_end, int base);

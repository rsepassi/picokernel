#include "string.h"

int memcmp(const void *s1, const void *s2, size_t n) {
  const unsigned char *p1 = (const unsigned char *)s1;
  const unsigned char *p2 = (const unsigned char *)s2;
  for (size_t i = 0; i < n; i++) {
    if (p1[i] != p2[i]) {
      return p1[i] - p2[i];
    }
  }
  return 0;
}

void *memchr(const void *s, int c, size_t n) {
  const unsigned char *p = (const unsigned char *)s;
  unsigned char target = (unsigned char)c;
  for (size_t i = 0; i < n; i++) {
    if (p[i] == target) {
      return (void *)(p + i);
    }
  }
  return NULL;
}

size_t strlen(const char *str) {
  size_t len = 0;
  while (str[len])
    len++;
  return len;
}

char *strrchr(const char *str, int ch) {
  const char *last = NULL;
  char c = (char)ch;

  while (*str) {
    if (*str == c) {
      last = str;
    }
    str++;
  }

  // Check if we're looking for null terminator
  if (c == '\0') {
    return (char *)str;
  }

  return (char *)last;
}

int strcmp(const char *s1, const char *s2) {
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

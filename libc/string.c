#include "string.h"

void *memcpy(void *dest, const void *src, size_t n) {
  unsigned char *d = (unsigned char *)dest;
  const unsigned char *s = (const unsigned char *)src;
  for (size_t i = 0; i < n; i++) {
    d[i] = s[i];
  }
  return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
  unsigned char *d = (unsigned char *)dest;
  const unsigned char *s = (const unsigned char *)src;

  if (d < s) {
    // Copy forward
    for (size_t i = 0; i < n; i++) {
      d[i] = s[i];
    }
  } else if (d > s) {
    // Copy backward to handle overlap
    for (size_t i = n; i > 0; i--) {
      d[i - 1] = s[i - 1];
    }
  }
  // If d == s, no copy needed

  return dest;
}

void *memset(void *s, int c, size_t n) {
  unsigned char *p = (unsigned char *)s;
  for (size_t i = 0; i < n; i++) {
    p[i] = (unsigned char)c;
  }
  return s;
}

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

size_t strnlen(const char *str, size_t maxlen) {
  size_t len = 0;
  while (len < maxlen && str[len])
    len++;
  return len;
}

char *strchr(const char *str, int ch) {
  char c = (char)ch;
  while (*str) {
    if (*str == c) {
      return (char *)str;
    }
    str++;
  }
  // Check if we're looking for null terminator
  if (c == '\0') {
    return (char *)str;
  }
  return NULL;
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

unsigned long strtoul(const char *str, char **str_end, int base) {
  const char *s = str;
  unsigned long result = 0;

  // Skip whitespace
  while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
    s++;
  }

  // Handle base prefix
  if (base == 0) {
    if (*s == '0') {
      if (s[1] == 'x' || s[1] == 'X') {
        base = 16;
        s += 2;
      } else {
        base = 8;
        s++;
      }
    } else {
      base = 10;
    }
  } else if (base == 16) {
    if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) {
      s += 2;
    }
  }

  // Convert digits
  while (*s) {
    int digit;
    if (*s >= '0' && *s <= '9') {
      digit = *s - '0';
    } else if (*s >= 'a' && *s <= 'z') {
      digit = *s - 'a' + 10;
    } else if (*s >= 'A' && *s <= 'Z') {
      digit = *s - 'A' + 10;
    } else {
      break;
    }

    if (digit >= base) {
      break;
    }

    result = result * base + digit;
    s++;
  }

  if (str_end) {
    *str_end = (char *)s;
  }

  return result;
}

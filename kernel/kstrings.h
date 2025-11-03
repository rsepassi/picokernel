// String Helper Utilities
// Foundation layer - no dependencies except standard headers

#ifndef KSTRINGS_H
#define KSTRINGS_H

#include <stddef.h>

// Compare two strings for equality
// Returns: 1 if equal, 0 otherwise
static inline int str_eql(const char *a, const char *b) {
  while (*a && *b) {
    if (*a != *b)
      return 0;
    a++;
    b++;
  }
  return *a == *b;
}

// Get string length
// Returns: length of string (excluding null terminator)
static inline size_t str_len(const char *str) {
  size_t len = 0;
  while (str[len])
    len++;
  return len;
}

// Check if string starts with prefix
// Returns: 1 if str starts with prefix, 0 otherwise
static inline int str_startswith(const char *str, const char *prefix) {
  while (*prefix) {
    if (*str != *prefix)
      return 0;
    str++;
    prefix++;
  }
  return 1;
}

// Check if string ends with suffix
// Returns: 1 if str ends with suffix, 0 otherwise
static inline int str_endswith(const char *str, const char *suffix) {
  size_t slen = str_len(str);
  size_t suffix_len = str_len(suffix);

  // Suffix longer than string
  if (suffix_len > slen)
    return 0;

  // Compare from end backwards
  const char *str_end = str + slen - suffix_len;
  return str_eql(str_end, suffix);
}

#endif // KSTRINGS_H

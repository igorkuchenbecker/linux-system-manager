#ifndef LSM_UTILS_STRUTILS_H
#define LSM_UTILS_STRUTILS_H

#include <stddef.h>

/*
 * BSD-style safe string copy: always NUL-terminates `dst` (given
 * dstsize > 0), never writes past dstsize, and returns strlen(src) so
 * callers can detect truncation (return >= dstsize means truncated).
 * glibc does not provide strlcpy() without linking libbsd, so we provide
 * our own rather than adding a dependency.
 */
size_t lsm_strlcpy(char *dst, const char *src, size_t dstsize);

/*
 * Trims leading and trailing ASCII whitespace from `s` in place
 * (including the newline left by fgets()/getline()). Returns a pointer
 * to the first non-whitespace character within `s` (may equal `s`).
 */
char *lsm_str_trim(char *s);

/* Returns 1 if `s` starts with `prefix`, 0 otherwise. Both must be non-NULL. */
int lsm_str_has_prefix(const char *s, const char *prefix);

#endif /* LSM_UTILS_STRUTILS_H */

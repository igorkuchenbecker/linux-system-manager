#include "utils/strutils.h"

#include <ctype.h>
#include <string.h>

size_t lsm_strlcpy(char *dst, const char *src, size_t dstsize)
{
    size_t src_len = strlen(src);

    if (dstsize > 0) {
        size_t copy_len = (src_len < dstsize - 1) ? src_len : dstsize - 1;
        memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
    }

    return src_len;
}

char *lsm_str_trim(char *s)
{
    while (*s != '\0' && isspace((unsigned char)*s))
        s++;

    if (*s == '\0')
        return s;

    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end))
        end--;
    end[1] = '\0';

    return s;
}

int lsm_str_has_prefix(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

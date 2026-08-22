#ifndef LSM_TEST_COMMON_H
#define LSM_TEST_COMMON_H

#include <stdio.h>
#include <stdlib.h>

/*
 * Minimal assert-based test harness, deliberately dependency-free (no
 * CUnit/Check/etc.) so `make test` never needs an extra package on top of
 * a plain C toolchain.
 */
#define TEST_ASSERT(cond, msg)                                              \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
            exit(1);                                                        \
        }                                                                   \
    } while (0)

#define TEST_OK(name) fprintf(stderr, "PASS %s\n", (name))

#endif /* LSM_TEST_COMMON_H */

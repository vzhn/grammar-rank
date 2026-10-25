#ifndef SELFTEST_H
#define SELFTEST_H
#include "table.h"

/* Enumerate every derivation tree by brute force and check it against the
 * table, rank and unrank; then round-trip indices.  Returns 0 on success. */
int selftest(Table *t);

#endif

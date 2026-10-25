/*
 * The bijection.  Strings are ordered by derivation size, then inside a
 * size class by: production of the root in grammar order, then the split
 * of the remaining size among the nonterminal children, then the children
 * left to right.
 */
#ifndef RANK_H
#define RANK_H
#include <gmp.h>
#include "table.h"
#include "tree.h"

void rank(Table *t, mpz_t r, const Node *n);      /* index of a derivation tree */
Node *unrank(Table *t, const mpz_t num);          /* derivation tree with that index */

#endif

/*
 * Counting tables.  N[A].v[s] is the number of derivation trees rooted at
 * nonterminal A with s production applications; suf[p][i].v[t] is the number
 * of ways the nonterminals i.. of production p can use t applications in
 * total; off[s] is the number of strings of the language with derivation
 * size below s, which is the index of the first string of size s.
 */
#ifndef TABLE_H
#define TABLE_H
#include <gmp.h>
#include "grammar.h"

typedef struct { mpz_t *v; int cap; } Tab;

typedef struct {
    const Grammar *g;
    int built;      /* sizes 0..built-1 are computed */
    Tab *N;         /* [nn] */
    Tab **suf;      /* [np][m+1] */
    Tab ***same;    /* [np][m+1]: an earlier production's suffix with the same
                       nonterminals, whose values suf[p][i] copies, or NULL */
    mpz_t *off;     /* [built+1] */
} Table;

void table_init(Table *t, const Grammar *g);
void table_ensure(Table *t, int S);    /* compute all sizes up to S */

#endif

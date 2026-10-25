/*
 * Counting table for shapes.  C(n, m, r, k) is the number of ways to finish
 * a shape with n actions left, m + 1 nonterminals introduced, r rules still
 * owed and k terminals introduced:
 *
 *   C(0, m, 0, k) = base(k),  C(n, m, 0, k) = 0 for n > 0,  and for n > 0, r > 0
 *   C(n, m, r, k) =  k * C(n-1, m, r, k)        old terminal
 *                 +      C(n-1, m, r, k+1)      new terminal
 *                 + (m+1) * C(n-1, m, r, k)     old nonterminal
 *                 +      C(n-1, m+1, r+1, k)    new nonterminal
 *                 +      C(n-1, m, r, k)        |
 *                 +      C(n-1, m, r-1, k)      ;
 *
 * With sigma < 0 the base is 1 and the table counts shapes.  With sigma >= 0
 * the base is P(sigma, k) = sigma!/(sigma-k)!, the number of ways to spell
 * k shape terminals with distinct terminals of an alphabet of size sigma,
 * and the table counts grammars over that alphabet.
 */
#ifndef SHAPETABLE_H
#define SHAPETABLE_H
#include <gmp.h>

typedef struct {
    int sigma;
    int N;          /* levels 0..N are built; -1 means nothing yet */
    mpz_t **lvl;    /* lvl[n]: entries of level n, see stab_at */
    int **mbase;    /* mbase[n][m]: start of the (m, *, *) block in lvl[n] */
    mpz_t *off;     /* off[n]: number of objects of size < n, n = 0..N+1 */
    mpz_t *perm;    /* perm[k] = P(sigma, k), k = 0..sigma; NULL for sigma < 0 */
    mpz_t zero, one;
} ShapeTable;

void stab_init(ShapeTable *t, int sigma);
void stab_ensure(ShapeTable *t, int N);              /* build levels 0..N */
mpz_srcptr stab_at(const ShapeTable *t, int n, int m, int r, int k);
mpz_srcptr stab_base(const ShapeTable *t, int k);    /* base(k) */
void stab_free(ShapeTable *t);

#endif

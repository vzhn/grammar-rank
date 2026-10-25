/*
 * Counting shapes.  C(n, m, r, k) is the number of ways to finish a shape
 * with n actions left, m + 1 nonterminals introduced, r rules still owed
 * and k terminals introduced.  Terminal actions interleave freely with the
 * nonterminal actions (only the final `;` is fixed), so
 *
 *   C(n, m, r, k) = sum_{a=0}^{n-1} binom(n-1, a) R(a, k) NT(n-a, m, r)
 *
 * where R(a, k) counts sequences of a terminal actions starting with k
 * terminals introduced, and NT(b, m, r) counts sequences of b nonterminal
 * actions (old or new nonterminal, |, ;) that end exactly when the last
 * owed rule is closed:
 *
 *   NT(0, m, 0) = 1,   NT(b, m, 0) = 0 for b > 0,
 *   NT(b, m, r) = (m+2) NT(b-1, m, r) + NT(b-1, m+1, r+1) + NT(b-1, m, r-1).
 *
 * With sigma < 0 the table counts shapes: R(a, k) = sum_i binom(a, i) k^i
 * Bell(a-i), the r-Bell numbers.  With sigma >= 0 every shape is weighted
 * by the number of spellings of its terminals over an alphabet of size
 * sigma, P(sigma, k) = sigma!/(sigma-k)!, so R(a, k) = P(sigma, k) sigma^a
 * and the table counts grammars over that alphabet.
 *
 * NT is never stored whole.  A pass streams its levels b = 0, 1, ... with
 * two (m, r) planes and records only the columns NT(., m, r) that were
 * requested with stab_mark(); stab_count() requests a band of columns
 * around (m, r) when it meets one that is missing.  A plane holds only the
 * states a shape can reach: m <= N - b and 2m + 1 - r <= N - b, since each
 * new nonterminal and each closed rule took an action.
 */
#ifndef SHAPETABLE_H
#define SHAPETABLE_H
#include <gmp.h>

typedef struct { int len; mpz_t *v; } Col;     /* v[b] = NT(b, m, r), b < len */

typedef struct {
    int sigma;
    int N;              /* sizes 0..N are supported; -1 before stab_ensure */
    mpz_t *bell;        /* [N+1] Bell numbers (sigma < 0) */
    mpz_t *pw;          /* [N+1] sigma^a (sigma >= 0) */
    mpz_t *perm;        /* [sigma+1] P(sigma, k) */
    mpz_t **R;          /* [N+2] R[k][a], a = 0..N, computed on first use */
    Col *col;           /* [(N+1)(N+2)] NT columns, col[m (N+2) + r] */
    int *want;          /* [(N+1)(N+2)] largest b requested, or -1 */
    mpz_t *plane[2];    /* (m, r) planes used by the passes */
    mpz_t *off;         /* [N+2] off[n]: objects of size < n */
    int have_off;
    mpz_t zero, one, t1, t2, t3;
} ShapeTable;

void stab_init(ShapeTable *t, int sigma);
void stab_ensure(ShapeTable *t, int N);               /* support sizes up to N */
void stab_mark(ShapeTable *t, int m, int r, int maxb); /* request NT(b, m, r) for b <= maxb */
void stab_compute(ShapeTable *t);                     /* one pass computing the requested columns */
void stab_forget(ShapeTable *t);                      /* drop all columns */
void stab_count(ShapeTable *t, mpz_t out, int n, int m, int r, int k);   /* C(n, m, r, k) */
mpz_srcptr stab_off(ShapeTable *t, int n);            /* objects of size < n, n <= N+1 */
void stab_total(ShapeTable *t, mpz_t out, int n);     /* objects of size n */
mpz_srcptr stab_base(const ShapeTable *t, int k);     /* R(0, k): the weight of a finished shape */
void stab_free(ShapeTable *t);

#endif

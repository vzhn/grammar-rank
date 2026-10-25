#include <stdlib.h>
#include "shapetable.h"
#include "util.h"

void stab_init(ShapeTable *t, int sigma)
{
    t->sigma = sigma;
    t->N = -1;
    t->lvl = NULL;
    t->mbase = NULL;
    t->off = NULL;
    t->perm = NULL;
    mpz_init(t->zero);
    mpz_init_set_ui(t->one, 1);
    if (sigma >= 0) {
        t->perm = xmalloc((size_t)(sigma + 1) * sizeof *t->perm);
        mpz_init_set_ui(t->perm[0], 1);
        for (int k = 1; k <= sigma; k++) {
            mpz_init(t->perm[k]);
            mpz_mul_ui(t->perm[k], t->perm[k - 1], (unsigned long)(sigma - k + 1));
        }
    }
}

mpz_srcptr stab_base(const ShapeTable *t, int k)
{
    if (t->sigma < 0) return t->one;
    return k <= t->sigma ? t->perm[k] : t->zero;
}

/* entries of level n: m in 0..L, k in 0..L-m, r in 0..m+1, where L = N - n */
static int level_size(int L, int **mbase)
{
    int *b = xmalloc((size_t)(L + 2) * sizeof *b);
    b[0] = 0;
    for (int m = 0; m <= L; m++) b[m + 1] = b[m] + (L - m + 1) * (m + 2);
    *mbase = b;
    return b[L + 1];
}

mpz_srcptr stab_at(const ShapeTable *t, int n, int m, int r, int k)
{
    if (r < 0 || r > m + 1) return t->zero;
    if (n < 0 || n > t->N || m < 0 || k < 0 || m + k > t->N - n)
        die("internal: shape table lookup (%d,%d,%d,%d) outside the table for N=%d", n, m, r, k, t->N);
    return t->lvl[n][t->mbase[n][m] + k * (m + 2) + r];
}

static void free_levels(ShapeTable *t)
{
    for (int n = 0; n <= t->N; n++) {
        int L = t->N - n;
        int size = t->mbase[n][L + 1];
        for (int i = 0; i < size; i++) mpz_clear(t->lvl[n][i]);
        free(t->lvl[n]);
        free(t->mbase[n]);
    }
    for (int n = 0; n <= t->N + 1; n++) mpz_clear(t->off[n]);
    free(t->lvl);
    free(t->mbase);
    free(t->off);
    t->lvl = NULL;
    t->mbase = NULL;
    t->off = NULL;
    t->N = -1;
}

void stab_ensure(ShapeTable *t, int N)
{
    if (N <= t->N) return;
    int step = t->N / 4 < 8 ? 8 : t->N / 4;     /* grow in steps: a rebuild costs O(N^4) */
    if (N < t->N + step) N = t->N + step;
    if (t->N >= 0) free_levels(t);
    t->N = N;
    t->lvl = xmalloc((size_t)(N + 1) * sizeof *t->lvl);
    t->mbase = xmalloc((size_t)(N + 1) * sizeof *t->mbase);
    for (int n = 0; n <= N; n++) {
        int L = N - n;
        int size = level_size(L, &t->mbase[n]);
        mpz_t *v = t->lvl[n] = xmalloc((size_t)size * sizeof *v);
        for (int i = 0; i < size; i++) mpz_init(v[i]);
        for (int m = 0; m <= L; m++)
            for (int k = 0; k + m <= L; k++)
                for (int r = 0; r <= m + 1; r++) {
                    mpz_ptr c = v[t->mbase[n][m] + k * (m + 2) + r];
                    if (n == 0) {
                        if (r == 0) mpz_set(c, stab_base(t, k));
                        continue;
                    }
                    if (r == 0) continue;
                    mpz_srcptr same = stab_at(t, n - 1, m, r, k);
                    mpz_addmul_ui(c, same, (unsigned long)(k + m + 2));   /* old terminal, old nonterminal, | */
                    mpz_add(c, c, stab_at(t, n - 1, m, r, k + 1));        /* new terminal */
                    mpz_add(c, c, stab_at(t, n - 1, m + 1, r + 1, k));    /* new nonterminal */
                    mpz_add(c, c, stab_at(t, n - 1, m, r - 1, k));        /* ; */
                }
    }
    t->off = xmalloc((size_t)(N + 2) * sizeof *t->off);
    mpz_init(t->off[0]);
    for (int n = 0; n <= N; n++) {
        mpz_init(t->off[n + 1]);
        mpz_add(t->off[n + 1], t->off[n], stab_at(t, n, 0, 1, 0));
    }
}

void stab_free(ShapeTable *t)
{
    if (t->N >= 0) free_levels(t);
    if (t->perm) {
        for (int k = 0; k <= t->sigma; k++) mpz_clear(t->perm[k]);
        free(t->perm);
    }
    mpz_clear(t->zero);
    mpz_clear(t->one);
}

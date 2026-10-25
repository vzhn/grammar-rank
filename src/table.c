#include <stdlib.h>
#include "table.h"
#include "util.h"

static void tab_grow(Tab *t, int need)
{
    if (t->cap >= need) return;
    int ncap = t->cap ? t->cap : 16;
    while (ncap < need) ncap *= 2;
    t->v = xrealloc(t->v, (size_t)ncap * sizeof *t->v);
    for (int i = t->cap; i < ncap; i++) mpz_init(t->v[i]);
    t->cap = ncap;
}

void table_init(Table *t, const Grammar *g)
{
    t->g = g;
    t->built = 0;
    t->N = xcalloc((size_t)g->nn, sizeof *t->N);
    t->suf = xmalloc((size_t)g->np * sizeof *t->suf);
    for (int p = 0; p < g->np; p++) t->suf[p] = xcalloc((size_t)g->prod[p].m + 1, sizeof(Tab));
    t->off = xmalloc(sizeof *t->off);
    mpz_init_set_ui(t->off[0], 0);
}

void table_ensure(Table *t, int S)
{
    if (S < t->built) return;
    const Grammar *g = t->g;

    t->off = xrealloc(t->off, (size_t)(S + 2) * sizeof *t->off);
    for (int s = t->built + 1; s <= S + 1; s++) mpz_init(t->off[s]);
    for (int a = 0; a < g->nn; a++) tab_grow(&t->N[a], S + 1);
    for (int p = 0; p < g->np; p++)
        for (int i = 0; i <= g->prod[p].m; i++) tab_grow(&t->suf[p][i], S + 1);

    mpz_t tmp;
    mpz_init(tmp);
    for (int s = t->built; s <= S; s++) {
        /* N_A(s) = sum over productions of A of suf[p][0](s - 1) */
        for (int a = 0; a < g->nn; a++) {
            mpz_ptr v = t->N[a].v[s];
            mpz_set_ui(v, 0);
            if (s == 0) continue;
            for (int k = 0; k < g->pcount[a]; k++)
                mpz_add(v, v, t->suf[g->plist[a][k]][0].v[s - 1]);
        }
        /* suf[p][i](s) = sum over j of N_X(j) * suf[p][i+1](s - j), X the i-th nonterminal */
        for (int p = 0; p < g->np; p++) {
            const Prod *pr = &g->prod[p];
            Tab *suf = t->suf[p];
            mpz_set_ui(suf[pr->m].v[s], s == 0 ? 1 : 0);
            for (int i = pr->m - 1; i >= 0; i--) {
                int X = pr->rhs[pr->ntpos[i]].id;
                mpz_ptr v = suf[i].v[s];
                mpz_set_ui(v, 0);
                for (int j = 1; j <= s; j++) {
                    mpz_mul(tmp, t->N[X].v[j], suf[i + 1].v[s - j]);
                    mpz_add(v, v, tmp);
                }
            }
        }
        mpz_add(t->off[s + 1], t->off[s], t->N[0].v[s]);
    }
    mpz_clear(tmp);
    t->built = S + 1;
}

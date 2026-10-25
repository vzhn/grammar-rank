#include <stdio.h>
#include <stdlib.h>
#include "rank.h"
#include "util.h"

static void rank_in(Table *tb, mpz_t r, const Node *n);

/* position of children i.. of n among all ways to spend t applications on them */
static void rank_suf(Table *tb, mpz_t r, const Node *n, int i, int t)
{
    const Tab *suf = tb->suf[n->prod];
    mpz_set_ui(r, 0);
    if (i == n->nkids) return;
    const Node *c = n->kids[i];
    int si = c->size, X = c->nt;
    mpz_t tmp;
    mpz_init(tmp);
    for (int j = 1; j < si; j++) {                       /* smaller first child */
        mpz_mul(tmp, tb->N[X].v[j], suf[i + 1].v[t - j]);
        mpz_add(r, r, tmp);
    }
    rank_in(tb, tmp, c);                                 /* same size, smaller child */
    mpz_mul(tmp, tmp, suf[i + 1].v[t - si]);
    mpz_add(r, r, tmp);
    rank_suf(tb, tmp, n, i + 1, t - si);                 /* same child, rest */
    mpz_add(r, r, tmp);
    mpz_clear(tmp);
}

/* position of n among all trees of its root and size */
static void rank_in(Table *tb, mpz_t r, const Node *n)
{
    const Grammar *g = tb->g;
    int t = n->size - 1;
    rank_suf(tb, r, n, 0, t);
    for (int k = 0; k < g->pcount[n->nt]; k++) {         /* earlier productions */
        int q = g->plist[n->nt][k];
        if (q == n->prod) break;
        mpz_add(r, r, tb->suf[q][0].v[t]);
    }
}

void rank(Table *tb, mpz_t r, const Node *n)
{
    table_ensure(tb, n->size);
    rank_in(tb, r, n);
    mpz_add(r, r, tb->off[n->size]);
}

static Node *unrank_in(Table *tb, mpz_t r, int A, int s);

static void unrank_suf(Table *tb, mpz_t r, Node *n, int i, int t)
{
    const Prod *pr = &tb->g->prod[n->prod];
    const Tab *suf = tb->suf[n->prod];
    if (i == n->nkids) return;
    int X = pr->rhs[pr->ntpos[i]].id;
    mpz_t c, q;
    mpz_inits(c, q, NULL);
    for (int j = 1; j <= t; j++) {
        mpz_ptr rest = suf[i + 1].v[t - j];
        mpz_mul(c, tb->N[X].v[j], rest);
        if (mpz_cmp(r, c) < 0) {
            mpz_fdiv_qr(q, r, r, rest);
            n->kids[i] = unrank_in(tb, q, X, j);
            unrank_suf(tb, r, n, i + 1, t - j);
            mpz_clears(c, q, NULL);
            return;
        }
        mpz_sub(r, r, c);
    }
    die("internal error: unrank_suf out of range");
}

static Node *unrank_in(Table *tb, mpz_t r, int A, int s)
{
    const Grammar *g = tb->g;
    int t = s - 1;
    for (int k = 0; k < g->pcount[A]; k++) {
        int q = g->plist[A][k];
        if (mpz_cmp(r, tb->suf[q][0].v[t]) < 0) {
            Node *n = node_new(g, A, q);
            unrank_suf(tb, r, n, 0, t);
            node_finish(n);
            return n;
        }
        mpz_sub(r, r, tb->suf[q][0].v[t]);
    }
    die("internal error: unrank_in out of range");
}

Node *unrank(Table *tb, const mpz_t num)
{
    const Grammar *g = tb->g;
    if (g->finite) {
        table_ensure(tb, g->max_size);
        if (mpz_cmp(num, tb->off[g->max_size + 1]) >= 0) {
            gmp_fprintf(stderr, "cfg: the language is finite and has only %Zd strings\n",
                        tb->off[g->max_size + 1]);
            exit(1);
        }
    }
    int s = 0;
    table_ensure(tb, 0);
    while (mpz_cmp(tb->off[s + 1], num) <= 0) { s++; table_ensure(tb, s); }
    mpz_t r;
    mpz_init(r);
    mpz_sub(r, num, tb->off[s]);
    Node *n = unrank_in(tb, r, 0, s);
    mpz_clear(r);
    return n;
}

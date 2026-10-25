#define _GNU_SOURCE   /* open_memstream */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "selftest.h"
#include "parser.h"
#include "rank.h"
#include "tree.h"
#include "util.h"

typedef struct { void **v; size_t n, cap; } Vec;

static void vec_push(Vec *v, void *x)
{
    if (v->n == v->cap) { v->cap = v->cap ? 2 * v->cap : 16; v->v = xrealloc(v->v, v->cap * sizeof *v->v); }
    v->v[v->n++] = x;
}

/* brute-force enumeration, memoized per (nonterminal, size); the memo owns the trees */
static struct { Vec **memo; unsigned char **done; int *cap; } M;

static const Vec *gen(const Grammar *g, int A, int s);

/* all ways to fill kids i.. of production p with t applications; each result is a Node*[m] */
static void gen_suf(const Grammar *g, Vec *out, int p, int i, int t)
{
    const Prod *pr = &g->prod[p];
    if (i == pr->m) {
        if (t == 0) vec_push(out, xcalloc((size_t)pr->m + 1, sizeof(Node *)));
        return;
    }
    int X = pr->rhs[pr->ntpos[i]].id;
    for (int j = 1; j <= t; j++) {
        const Vec *kids = gen(g, X, j);
        if (!kids->n) continue;
        Vec rests = {0};
        gen_suf(g, &rests, p, i + 1, t - j);
        for (size_t a = 0; a < kids->n; a++)
            for (size_t b = 0; b < rests.n; b++) {
                Node **arr = xcalloc((size_t)pr->m + 1, sizeof *arr);
                Node **rest = rests.v[b];
                for (int k = i + 1; k < pr->m; k++) arr[k] = node_copy(rest[k]);
                arr[i] = node_copy(kids->v[a]);
                vec_push(out, arr);
            }
        for (size_t b = 0; b < rests.n; b++) {
            Node **arr = rests.v[b];
            for (int k = i + 1; k < pr->m; k++) node_free(arr[k]);
            free(arr);
        }
        free(rests.v);
    }
}

static const Vec *gen(const Grammar *g, int A, int s)
{
    static const Vec empty = {0};
    if (s <= 0) return &empty;
    if (!M.memo) {
        M.memo = xcalloc((size_t)g->nn, sizeof *M.memo);
        M.done = xcalloc((size_t)g->nn, sizeof *M.done);
        M.cap = xcalloc((size_t)g->nn, sizeof *M.cap);
    }
    if (s >= M.cap[A]) {
        int ncap = M.cap[A] ? M.cap[A] : 16;
        while (ncap <= s) ncap *= 2;
        M.memo[A] = xrealloc(M.memo[A], (size_t)ncap * sizeof **M.memo);
        M.done[A] = xrealloc(M.done[A], (size_t)ncap);
        memset(M.memo[A] + M.cap[A], 0, (size_t)(ncap - M.cap[A]) * sizeof **M.memo);
        memset(M.done[A] + M.cap[A], 0, (size_t)(ncap - M.cap[A]));
        M.cap[A] = ncap;
    }
    Vec *out = &M.memo[A][s];
    if (M.done[A][s]) return out;
    for (int k = 0; k < g->pcount[A]; k++) {
        int p = g->plist[A][k];
        Vec fills = {0};
        gen_suf(g, &fills, p, 0, s - 1);
        for (size_t f = 0; f < fills.n; f++) {
            Node *n = node_new(g, A, p);
            Node **arr = fills.v[f];
            for (int i = 0; i < n->nkids; i++) n->kids[i] = arr[i];
            free(arr);
            node_finish(n);
            vec_push(out, n);
        }
        free(fills.v);
    }
    M.done[A][s] = 1;
    return out;
}

static int failures;

static void fail(const Grammar *g, const char *what, const Node *n)
{
    failures++;
    fprintf(stderr, "FAIL: %s", what);
    if (n) { fputs(": ", stderr); print_tree(g, n, stderr); }
    fputc('\n', stderr);
}

/* print the string, tokenize it again, parse it again, compare trees */
static void check_reparse(const Grammar *g, const Node *t)
{
    char *buf = NULL;
    size_t bl = 0;
    FILE *mf = open_memstream(&buf, &bl);
    print_string(g, t, mf);
    fclose(mf);
    Tokens tk;
    tokenize(g, buf, &tk);
    Node *q = parse(g, &tk);
    if (!node_eq(q, t)) fail(g, "string does not reparse to the same tree", t);
    node_free(q);
    tokens_free(&tk);
    free(buf);
}

int selftest(Table *tb)
{
    const Grammar *g = tb->g;
    mpz_t r, want;
    mpz_inits(r, want, NULL);
    unsigned long total = 0;

    for (int s = 0; s <= 60 && total < 5000 && !(g->finite && s > g->max_size); s++) {
        const Vec *all = gen(g, 0, s);
        table_ensure(tb, s);
        if (mpz_cmp_ui(tb->N[0].v[s], (unsigned long)all->n) != 0)
            fail(g, "count disagrees with brute force", NULL);
        for (size_t i = 0; i < all->n; i++) {
            Node *t = all->v[i];
            rank(tb, r, t);
            mpz_add_ui(want, tb->off[s], (unsigned long)i);
            if (mpz_cmp(r, want) != 0) fail(g, "rank differs from brute-force position", t);
            Node *u = unrank(tb, want);
            if (!node_eq(u, t)) fail(g, "unrank differs from brute-force tree", t);
            node_free(u);
            check_reparse(g, t);
        }
        printf("size %2d: %6zu strings ok\n", s, all->n);
        total += all->n;
    }

    unsigned long limit = 20000;
    if (g->finite) {
        table_ensure(tb, g->max_size);
        if (mpz_cmp_ui(tb->off[g->max_size + 1], limit) < 0) limit = mpz_get_ui(tb->off[g->max_size + 1]);
    }
    for (unsigned long i = 0; i < limit; i++) {
        mpz_set_ui(want, i);
        Node *t = unrank(tb, want);
        rank(tb, r, t);
        if (mpz_cmp(r, want) != 0) fail(g, "rank(unrank(i)) != i", t);
        node_free(t);
    }
    printf("round trip 0..%lu ok\n", limit ? limit - 1 : 0);

    if (!g->finite) {
        const char *big[] = { "1000000000000", "123456789012345678901234567890",
                              "99999999999999999999999999999999999999999999999999" };
        for (size_t i = 0; i < sizeof big / sizeof *big; i++) {
            mpz_set_str(want, big[i], 10);
            Node *t = unrank(tb, want);
            rank(tb, r, t);
            if (mpz_cmp(r, want) != 0) fail(g, "big round trip", t);
            printf("%s -> size %d string ok\n", big[i], t->size);
            node_free(t);
        }
    }
    mpz_clears(r, want, NULL);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("selftest OK");
    return 0;
}

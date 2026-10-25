#include <stdlib.h>
#include <string.h>
#include "enumerate.h"
#include "util.h"

typedef struct { void **v; size_t n, cap; } Vec;

static void vec_push(Vec *v, void *x)
{
    if (v->n == v->cap) { v->cap = v->cap ? 2 * v->cap : 16; v->v = xrealloc(v->v, v->cap * sizeof *v->v); }
    v->v[v->n++] = x;
}

static struct { TreeList **memo; unsigned char **done; int *cap; const Grammar *g; } M;

/* all ways to fill kids i.. of production p with t applications; each result
 * is a Node*[m].  Memoized trees are never modified or freed, so a tree
 * points at its children's memoized trees instead of copying them: the trees
 * form a DAG and cost one node each. */
static void gen_suf(const Grammar *g, Vec *out, int p, int i, int t)
{
    const Prod *pr = &g->prod[p];
    if (i == pr->m) {
        if (t == 0) vec_push(out, xcalloc((size_t)pr->m + 1, sizeof(Node *)));
        return;
    }
    int X = pr->rhs[pr->ntpos[i]].id;
    for (int j = 1; j <= t; j++) {
        const TreeList *kids = enumerate(g, X, j);
        if (!kids->n) continue;
        Vec rests = {0};
        gen_suf(g, &rests, p, i + 1, t - j);
        for (size_t a = 0; a < kids->n; a++)
            for (size_t b = 0; b < rests.n; b++) {
                Node **arr = xcalloc((size_t)pr->m + 1, sizeof *arr);
                Node **rest = rests.v[b];
                for (int k = i + 1; k < pr->m; k++) arr[k] = rest[k];
                arr[i] = kids->v[a];
                vec_push(out, arr);
            }
        for (size_t b = 0; b < rests.n; b++) free(rests.v[b]);
        free(rests.v);
    }
}

const TreeList *enumerate(const Grammar *g, int A, int s)
{
    static const TreeList empty = {0};
    if (s <= 0) return &empty;
    if (M.g != g) {
        M.g = g;
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
    TreeList *out = &M.memo[A][s];
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
            if (out->n == out->cap) { out->cap = out->cap ? 2 * out->cap : 16; out->v = xrealloc(out->v, out->cap * sizeof *out->v); }
            out->v[out->n++] = n;
        }
        free(fills.v);
    }
    M.done[A][s] = 1;
    return out;
}

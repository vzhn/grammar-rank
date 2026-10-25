#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "earley.h"
#include "util.h"

/*
 * Items are (production, dot, origin) in set j.  Every way an item was
 * reached is kept as a derivation record: the predecessor item (same
 * production, dot one less) and the completed child item that advanced the
 * dot over a nonterminal, or -1 when a terminal was scanned.  From the
 * records the number of derivations is counted (capped at two) and the tree
 * is rebuilt.  Nullable nonterminals are handled by completing them at the
 * moment they are predicted, in both orders, so that no derivation is lost.
 */

typedef struct { int pred, child; } Der;

typedef struct {
    int prod, dot, origin;
    Der *der; int nder, dcap;
    int ways;              /* -1 unknown, -2 being computed */
} Item;

typedef struct {
    const Grammar *g;
    const Tokens *tk;
    Item *it; int nit, cap;
    int **set; int *nset, *scap;
} Earley;

static int find_item(const Earley *e, int j, int prod, int dot, int origin)
{
    for (int x = 0; x < e->nset[j]; x++) {
        const Item *it = &e->it[e->set[j][x]];
        if (it->prod == prod && it->dot == dot && it->origin == origin) return e->set[j][x];
    }
    return -1;
}

static int add_item(Earley *e, int j, int prod, int dot, int origin, int pred, int child)
{
    int id = find_item(e, j, prod, dot, origin);
    if (id < 0) {
        if (e->nit == e->cap) { e->cap = e->cap ? 2 * e->cap : 256; e->it = xrealloc(e->it, (size_t)e->cap * sizeof *e->it); }
        id = e->nit++;
        e->it[id] = (Item){ prod, dot, origin, NULL, 0, 0, -1 };
        if (e->nset[j] == e->scap[j]) { e->scap[j] = e->scap[j] ? 2 * e->scap[j] : 32; e->set[j] = xrealloc(e->set[j], (size_t)e->scap[j] * sizeof **e->set); }
        e->set[j][e->nset[j]++] = id;
    }
    if (pred < 0) return id;
    Item *it = &e->it[id];
    for (int d = 0; d < it->nder; d++)
        if (it->der[d].pred == pred && it->der[d].child == child) return id;
    if (it->nder == it->dcap) { it->dcap = it->dcap ? 2 * it->dcap : 4; it->der = xrealloc(it->der, (size_t)it->dcap * sizeof *it->der); }
    it->der[it->nder++] = (Der){ pred, child };
    return id;
}

static int ways(Earley *e, int id)
{
    Item *it = &e->it[id];
    if (it->ways >= 0) return it->ways;
    if (it->ways == -2) return 2;          /* a cycle: infinitely many */
    if (it->dot == 0) return it->ways = 1;
    it->ways = -2;
    int w = 0;
    for (int d = 0; d < it->nder; d++) {
        int wp = ways(e, it->der[d].pred);
        int wc = it->der[d].child < 0 ? 1 : ways(e, it->der[d].child);
        w += wp * wc;
        if (w > 2) w = 2;
    }
    e->it[id].ways = w;
    return w;
}

static Node *build_node(Earley *e, int id);

/* child nodes of the derivation of item id, left to right (nonterminals only) */
static void build_kids(Earley *e, int id, Node ***kids, int *nk, int *cap)
{
    const Item *it = &e->it[id];
    if (it->dot == 0) return;
    Der d = it->der[0];
    build_kids(e, d.pred, kids, nk, cap);
    if (d.child < 0) return;
    if (*nk == *cap) { *cap = *cap ? 2 * *cap : 8; *kids = xrealloc(*kids, (size_t)*cap * sizeof **kids); }
    (*kids)[(*nk)++] = build_node(e, d.child);
}

static Node *build_node(Earley *e, int id)
{
    const Item *it = &e->it[id];
    Node *n = node_new(e->g, e->g->prod[it->prod].lhs, it->prod);
    Node **kids = NULL;
    int nk = 0, cap = 0;
    build_kids(e, id, &kids, &nk, &cap);
    if (nk != n->nkids) die("internal error: Earley tree has %d children, production has %d", nk, n->nkids);
    for (int i = 0; i < nk; i++) n->kids[i] = kids[i];
    free(kids);
    node_finish(n);
    return n;
}

static void print_input(const Grammar *g, const Tokens *tk, FILE *f)
{
    for (int i = 0; i < tk->n; i++) fprintf(f, i ? " %s" : "%s", g->tname[tk->v[i]]);
}

Node *earley_parse(const Grammar *g, const Tokens *tk)
{
    int n = tk->n;
    Earley e = { g, tk, NULL, 0, 0, NULL, NULL, NULL };
    e.set = xcalloc((size_t)n + 1, sizeof *e.set);
    e.nset = xcalloc((size_t)n + 1, sizeof *e.nset);
    e.scap = xcalloc((size_t)n + 1, sizeof *e.scap);

    for (int k = 0; k < g->pcount[0]; k++) add_item(&e, 0, g->plist[0][k], 0, 0, -1, -1);

    for (int j = 0; j <= n; j++) {
        for (int x = 0; x < e.nset[j]; x++) {
            int id = e.set[j][x];
            int prod = e.it[id].prod, dot = e.it[id].dot, origin = e.it[id].origin;
            const Prod *pr = &g->prod[prod];
            if (dot < pr->len) {
                Ref sym = pr->rhs[dot];
                if (!sym.is_nt) {
                    if (j < n && tk->v[j] == sym.id) add_item(&e, j + 1, prod, dot + 1, origin, id, -1);
                    continue;
                }
                int B = sym.id;
                for (int k = 0; k < g->pcount[B]; k++) add_item(&e, j, g->plist[B][k], 0, j, -1, -1);
                /* B may already have been completed over the empty string at j */
                for (int y = 0; y < e.nset[j]; y++) {
                    int cid = e.set[j][y];
                    const Item *c = &e.it[cid];
                    const Prod *cp = &g->prod[c->prod];
                    if (c->origin == j && c->dot == cp->len && cp->lhs == B)
                        add_item(&e, j, prod, dot + 1, origin, id, cid);
                }
            } else {
                int A = pr->lhs, k = origin;
                for (int y = 0; y < e.nset[k]; y++) {
                    int tid = e.set[k][y];
                    const Item *t = &e.it[tid];
                    const Prod *tp = &g->prod[t->prod];
                    if (t->dot < tp->len && tp->rhs[t->dot].is_nt && tp->rhs[t->dot].id == A)
                        add_item(&e, j, t->prod, t->dot + 1, t->origin, tid, id);
                }
            }
        }
        if (j < n && e.nset[j + 1] == 0)
            die("syntax error at token %d (%s)", j + 1, g->tname[tk->v[j]]);
    }

    int total = 0, chosen = -1;
    for (int x = 0; x < e.nset[n]; x++) {
        int id = e.set[n][x];
        const Item *it = &e.it[id];
        const Prod *pr = &g->prod[it->prod];
        if (pr->lhs != 0 || it->origin != 0 || it->dot != pr->len) continue;
        total += ways(&e, id);
        if (total > 2) total = 2;
        if (chosen < 0) chosen = id;
    }
    if (total == 0) die("syntax error: unexpected end of input");
    if (total > 1) {
        fputs("cfg: ambiguous: '", stderr);
        print_input(g, tk, stderr);
        fputs("' has more than one derivation\n", stderr);
        exit(1);
    }
    Node *root = build_node(&e, chosen);

    for (int i = 0; i < e.nit; i++) free(e.it[i].der);
    for (int j = 0; j <= n; j++) free(e.set[j]);
    free(e.it); free(e.set); free(e.nset); free(e.scap);
    return root;
}

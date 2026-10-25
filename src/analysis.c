#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "analysis.h"
#include "ambiguity.h"
#include "util.h"

static unsigned char **alloc_sets(int rows, int cols)
{
    unsigned char **s = xmalloc((size_t)rows * sizeof *s);
    for (int i = 0; i < rows; i++) s[i] = xcalloc((size_t)cols, 1);
    return s;
}

/* add FIRST of rhs[from..] to dst; returns 1 if that suffix is nullable */
static int first_of_suffix(const Grammar *g, unsigned char *dst, const Prod *p, int from)
{
    for (int k = from; k < p->len; k++) {
        Ref r = p->rhs[k];
        if (!r.is_nt) { dst[r.id] = 1; return 0; }
        for (int t = 0; t < g->nt; t++) dst[t] |= g->first[r.id][t];
        if (!g->nullable[r.id]) return 0;
    }
    return 1;
}

static void compute_sets(Grammar *g)
{
    int nn = g->nn, nt = g->nt, np = g->np;
    g->nullable = xcalloc((size_t)nn, 1);
    g->productive = xcalloc((size_t)nn, 1);
    g->reachable = xcalloc((size_t)nn, 1);
    g->first = alloc_sets(nn, nt);
    g->follow = alloc_sets(nn, nt + 1);
    g->pred = alloc_sets(np, nt + 1);

    for (int changed = 1; changed;) {
        changed = 0;
        for (int p = 0; p < np; p++) {
            Prod *pr = &g->prod[p];
            int nul = 1, prodv = 1;
            for (int k = 0; k < pr->len; k++) {
                if (!pr->rhs[k].is_nt) { nul = 0; continue; }
                if (!g->nullable[pr->rhs[k].id]) nul = 0;
                if (!g->productive[pr->rhs[k].id]) prodv = 0;
            }
            if (nul && !g->nullable[pr->lhs]) { g->nullable[pr->lhs] = 1; changed = 1; }
            if (prodv && !g->productive[pr->lhs]) { g->productive[pr->lhs] = 1; changed = 1; }
        }
    }

    unsigned char *tmp = xmalloc((size_t)nt + 1);
    for (int changed = 1; changed;) {
        changed = 0;
        for (int p = 0; p < np; p++) {
            memset(tmp, 0, (size_t)nt + 1);
            first_of_suffix(g, tmp, &g->prod[p], 0);
            for (int t = 0; t < nt; t++)
                if (tmp[t] && !g->first[g->prod[p].lhs][t]) { g->first[g->prod[p].lhs][t] = 1; changed = 1; }
        }
    }

    g->follow[0][nt] = 1;
    for (int changed = 1; changed;) {
        changed = 0;
        for (int p = 0; p < np; p++) {
            Prod *pr = &g->prod[p];
            for (int k = 0; k < pr->len; k++) {
                if (!pr->rhs[k].is_nt) continue;
                int X = pr->rhs[k].id;
                memset(tmp, 0, (size_t)nt + 1);
                if (first_of_suffix(g, tmp, pr, k + 1))
                    for (int t = 0; t <= nt; t++) tmp[t] |= g->follow[pr->lhs][t];
                for (int t = 0; t <= nt; t++)
                    if (tmp[t] && !g->follow[X][t]) { g->follow[X][t] = 1; changed = 1; }
            }
        }
    }
    free(tmp);

    for (int p = 0; p < np; p++)
        if (first_of_suffix(g, g->pred[p], &g->prod[p], 0))
            for (int t = 0; t <= nt; t++) g->pred[p][t] |= g->follow[g->prod[p].lhs][t];

    g->reachable[0] = 1;
    for (int changed = 1; changed;) {
        changed = 0;
        for (int p = 0; p < np; p++) {
            if (!g->reachable[g->prod[p].lhs]) continue;
            for (int k = 0; k < g->prod[p].len; k++)
                if (g->prod[p].rhs[k].is_nt && !g->reachable[g->prod[p].rhs[k].id]) {
                    g->reachable[g->prod[p].rhs[k].id] = 1; changed = 1;
                }
        }
    }
}

/* LL(1): predict sets of the alternatives of one nonterminal are disjoint;
 * the first conflict is recorded for `check` */
static void check_ll1(Grammar *g)
{
    g->ll1_ok = 1;
    g->conflict_nt = -1;
    for (int a = 0; a < g->nn; a++)
        for (int x = 0; x < g->pcount[a]; x++)
            for (int y = x + 1; y < g->pcount[a]; y++)
                for (int t = 0; t <= g->nt; t++)
                    if (g->pred[g->plist[a][x]][t] && g->pred[g->plist[a][y]][t]) {
                        if (g->ll1_ok) {
                            g->conflict_nt = a; g->conflict_x = x; g->conflict_y = y; g->conflict_t = t;
                        }
                        g->ll1_ok = 0;
                        return;
                    }
}

/* a production is live if all its nonterminals are productive */
static int prod_live(const Grammar *g, const Prod *pr)
{
    for (int j = 0; j < pr->m; j++)
        if (!g->productive[pr->rhs[pr->ntpos[j]].id]) return 0;
    return 1;
}

/* the language is finite iff the graph of useful nonterminals is acyclic */
static int has_cycle(const Grammar *g)
{
    int nn = g->nn;
    int *color = xcalloc((size_t)nn, sizeof *color);   /* 0 white, 1 grey, 2 black */
    int *stack = xmalloc((size_t)nn * sizeof *stack);
    int *iter = xmalloc((size_t)nn * sizeof *iter);
    int cyclic = 0;
    for (int root = 0; root < nn && !cyclic; root++) {
        if (color[root] || !g->productive[root] || !g->reachable[root]) continue;
        int sp = 0;
        stack[sp] = root; iter[sp] = 0; color[root] = 1; sp++;
        while (sp && !cyclic) {
            int a = stack[sp - 1];
            /* the iter[sp-1]-th nonterminal child of a across its live productions */
            int found = -1, idx = 0;
            for (int k = 0; k < g->pcount[a] && found < 0; k++) {
                const Prod *pr = &g->prod[g->plist[a][k]];
                if (!prod_live(g, pr)) continue;
                for (int j = 0; j < pr->m; j++, idx++)
                    if (idx == iter[sp - 1]) { found = pr->rhs[pr->ntpos[j]].id; break; }
            }
            if (found < 0) { color[a] = 2; sp--; continue; }
            iter[sp - 1]++;
            if (color[found] == 1) cyclic = 1;
            else if (color[found] == 0) { color[found] = 1; stack[sp] = found; iter[sp] = 0; sp++; }
        }
    }
    free(color); free(stack); free(iter);
    return cyclic;
}

/* largest derivation size of the start symbol; the useful part is acyclic */
static int max_derivation_size(const Grammar *g)
{
    int nn = g->nn;
    int *ms = xmalloc((size_t)nn * sizeof *ms);
    for (int a = 0; a < nn; a++) ms[a] = -1;
    for (int changed = 1; changed;) {
        changed = 0;
        for (int a = 0; a < nn; a++) {
            if (ms[a] >= 0 || !g->productive[a]) continue;
            int best = -1, ok = 1;
            for (int k = 0; k < g->pcount[a] && ok; k++) {
                const Prod *pr = &g->prod[g->plist[a][k]];
                if (!prod_live(g, pr)) continue;
                int s = 1;
                for (int j = 0; j < pr->m; j++) {
                    int X = pr->rhs[pr->ntpos[j]].id;
                    if (ms[X] < 0) { ok = 0; break; }
                    s += ms[X];
                }
                if (ok && s > best) best = s;
            }
            if (ok && best >= 0) { ms[a] = best; changed = 1; }
        }
    }
    int result = g->productive[0] ? ms[0] : 0;
    free(ms);
    return result;
}

void analyze(Grammar *g)
{
    compute_sets(g);
    check_ll1(g);
    g->finite = !has_cycle(g);
    g->max_size = g->finite ? max_derivation_size(g) : 0;
    ambiguity_check(g);
}

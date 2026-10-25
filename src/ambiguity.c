#define _GNU_SOURCE   /* open_memstream */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ambiguity.h"
#include "enumerate.h"
#include "tree.h"
#include "util.h"

#define PROBE_TREES 2000
#define PROBE_MAX_SIZE 60

/* --- A =>+ A: edges A -> B for productions A -> alpha B beta with alpha, beta nullable --- */

static int find_cycle(const Grammar *g)
{
    int nn = g->nn;
    int *color = xcalloc((size_t)nn, sizeof *color);
    int *stack = xmalloc((size_t)nn * sizeof *stack);
    int *iter = xmalloc((size_t)nn * sizeof *iter);
    int found = -1;
    for (int root = 0; root < nn && found < 0; root++) {
        if (color[root]) continue;
        int sp = 0;
        stack[sp] = root; iter[sp] = 0; color[root] = 1; sp++;
        while (sp && found < 0) {
            int a = stack[sp - 1];
            /* the iter-th "unit" successor of a */
            int next = -1, idx = 0;
            for (int k = 0; k < g->pcount[a] && next < 0; k++) {
                const Prod *pr = &g->prod[g->plist[a][k]];
                int all_nullable = 1;
                for (int j = 0; j < pr->len; j++)
                    if (!pr->rhs[j].is_nt || !g->nullable[pr->rhs[j].id]) { all_nullable = 0; break; }
                for (int j = 0; j < pr->len && next < 0; j++) {
                    if (!pr->rhs[j].is_nt) continue;
                    /* every other symbol must be nullable */
                    int ok = all_nullable;
                    if (!ok) {
                        ok = 1;
                        for (int i = 0; i < pr->len; i++)
                            if (i != j && (!pr->rhs[i].is_nt || !g->nullable[pr->rhs[i].id])) { ok = 0; break; }
                    }
                    if (!ok) continue;
                    if (idx++ == iter[sp - 1]) next = pr->rhs[j].id;
                }
            }
            if (next < 0) { color[a] = 2; sp--; continue; }
            iter[sp - 1]++;
            if (color[next] == 1) found = next;
            else if (color[next] == 0) { color[next] = 1; stack[sp] = next; iter[sp] = 0; sp++; }
        }
    }
    free(color); free(stack); free(iter);
    return found;
}

/* --- probe: two enumerated trees with the same yield --- */

typedef struct Entry { char *key; const Node *tree; struct Entry *next; } Entry;

static unsigned long hash(const char *s)
{
    unsigned long h = 5381;
    while (*s) h = h * 33 + (unsigned char)*s++;
    return h;
}

static char *tree_text(const Grammar *g, const Node *n)
{
    char *buf = NULL;
    size_t len = 0;
    FILE *f = open_memstream(&buf, &len);
    print_tree(g, n, f);
    fclose(f);
    return buf;
}

static char *string_text(const Grammar *g, const Node *n)
{
    char *buf = NULL;
    size_t len = 0;
    FILE *f = open_memstream(&buf, &len);
    print_string(g, n, f);
    fclose(f);
    return buf;
}

static void probe(Grammar *g)
{
    enum { NB = 4093 };
    Entry **tab = xcalloc(NB, sizeof *tab);
    long total = 0;
    int s;
    for (s = 0; s <= PROBE_MAX_SIZE && total < PROBE_TREES && !(g->finite && s > g->max_size) && !g->ambiguous; s++) {
        const TreeList *all = enumerate(g, 0, s);
        for (size_t i = 0; i < all->n && !g->ambiguous; i++) {
            char *key = yield_key(g, all->v[i]);
            unsigned long h = hash(key) % NB;
            Entry *e;
            for (e = tab[h]; e; e = e->next) if (!strcmp(e->key, key)) break;
            if (e) {
                g->ambiguous = 1;
                g->ambig_string = string_text(g, all->v[i]);
                g->ambig_tree1 = tree_text(g, e->tree);
                g->ambig_tree2 = tree_text(g, all->v[i]);
                free(key);
            } else {
                e = xmalloc(sizeof *e);
                e->key = key; e->tree = all->v[i]; e->next = tab[h];
                tab[h] = e;
            }
        }
        total += (long)all->n;
    }
    g->probe_trees = total;
    g->probe_size = s - 1;
    g->probe_exhaustive = g->finite && s > g->max_size;
    for (int i = 0; i < NB; i++)
        for (Entry *e = tab[i]; e;) { Entry *nx = e->next; free(e->key); free(e); e = nx; }
    free(tab);
}

void ambiguity_check(Grammar *g)
{
    g->cyclic = -1;
    g->ambiguous = 0;
    if (g->ll1_ok) return;              /* LL(1) grammars are unambiguous */
    g->cyclic = find_cycle(g);
    if (g->cyclic >= 0) return;
    probe(g);
}

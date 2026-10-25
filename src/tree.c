#include <stdlib.h>
#include "tree.h"
#include "util.h"

static Node *alloc_node(int nt, int prod, int nkids)
{
    Node *n = xmalloc(sizeof *n);
    n->nt = nt; n->prod = prod; n->nkids = nkids; n->size = 0;
    n->kids = xcalloc((size_t)nkids, sizeof *n->kids);
    return n;
}

Node *node_new(const Grammar *g, int nt, int prod)
{
    return alloc_node(nt, prod, g->prod[prod].m);
}

void node_finish(Node *n)
{
    n->size = 1;
    for (int i = 0; i < n->nkids; i++) n->size += n->kids[i]->size;
}

void node_free(Node *n)
{
    if (!n) return;
    for (int i = 0; i < n->nkids; i++) node_free(n->kids[i]);
    free(n->kids);
    free(n);
}

Node *node_copy(const Node *n)
{
    Node *c = alloc_node(n->nt, n->prod, n->nkids);
    for (int i = 0; i < n->nkids; i++) c->kids[i] = node_copy(n->kids[i]);
    c->size = n->size;
    return c;
}

int node_eq(const Node *a, const Node *b)
{
    if (a->nt != b->nt || a->prod != b->prod) return 0;
    for (int i = 0; i < a->nkids; i++)
        if (!node_eq(a->kids[i], b->kids[i])) return 0;
    return 1;
}

int string_is_empty(const Grammar *g, const Node *n)
{
    const Prod *pr = &g->prod[n->prod];
    int ki = 0;
    for (int k = 0; k < pr->len; k++) {
        if (!pr->rhs[k].is_nt) return 0;
        if (!string_is_empty(g, n->kids[ki++])) return 0;
    }
    return 1;
}

static void print_leaves(const Grammar *g, const Node *n, FILE *f, int *first)
{
    const Prod *pr = &g->prod[n->prod];
    int ki = 0;
    for (int k = 0; k < pr->len; k++) {
        if (pr->rhs[k].is_nt) { print_leaves(g, n->kids[ki++], f, first); continue; }
        fprintf(f, *first ? "%s" : " %s", g->tname[pr->rhs[k].id]);
        *first = 0;
    }
}

void print_string(const Grammar *g, const Node *n, FILE *f)
{
    int first = 1;
    print_leaves(g, n, f, &first);
}

void print_tree(const Grammar *g, const Node *n, FILE *f)
{
    const Prod *pr = &g->prod[n->prod];
    fprintf(f, "[%s", g->ntname[n->nt]);
    if (pr->len == 0) fputs(" eps", f);
    int ki = 0;
    for (int k = 0; k < pr->len; k++) {
        fputc(' ', f);
        if (pr->rhs[k].is_nt) print_tree(g, n->kids[ki++], f);
        else fprintf(f, "'%s'", g->tname[pr->rhs[k].id]);
    }
    fputc(']', f);
}

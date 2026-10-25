#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shape.h"
#include "util.h"

void shape_init(Shape *s)
{
    s->a = NULL;
    s->n = s->cap = 0;
    s->nn = 1;        /* the start symbol is introduced before the first action */
    s->nt = 0;
}

void shape_push(Shape *s, Kind kind, int id)
{
    if (s->n == s->cap) {
        s->cap = s->cap ? 2 * s->cap : 32;
        s->a = xrealloc(s->a, (size_t)s->cap * sizeof *s->a);
    }
    if (kind == A_T) {
        if (id > s->nt) die("internal: shape terminal %d introduced before %d", id, s->nt);
        if (id == s->nt) s->nt++;
    } else if (kind == A_N) {
        if (id > s->nn) die("internal: shape nonterminal %d introduced before %d", id, s->nn);
        if (id == s->nn) s->nn++;
    }
    s->a[s->n].kind = kind;
    s->a[s->n].id = id;
    s->n++;
}

void shape_copy(Shape *dst, const Shape *src)
{
    dst->n = dst->cap = src->n;
    dst->nn = src->nn;
    dst->nt = src->nt;
    dst->a = xmalloc((size_t)(src->n ? src->n : 1) * sizeof *dst->a);
    memcpy(dst->a, src->a, (size_t)src->n * sizeof *dst->a);
}

void shape_free(Shape *s)
{
    free(s->a);
    s->a = NULL;
    s->n = s->cap = 0;
}

int shape_eq(const Shape *a, const Shape *b)
{
    if (a->n != b->n) return 0;
    for (int i = 0; i < a->n; i++)
        if (a->a[i].kind != b->a[i].kind || a->a[i].id != b->a[i].id) return 0;
    return 1;
}

int shape_of_grammar(const Grammar *g, Shape *s, int **tfirst, int **nfirst)
{
    int *nmap = xmalloc((size_t)g->nn * sizeof *nmap);
    int *order = xmalloc((size_t)g->nn * sizeof *order);
    int *tmap = xmalloc((size_t)(g->nt ? g->nt : 1) * sizeof *tmap);
    int *tord = xmalloc((size_t)(g->nt ? g->nt : 1) * sizeof *tord);
    for (int i = 0; i < g->nn; i++) nmap[i] = -1;
    for (int t = 0; t < g->nt; t++) tmap[t] = -1;

    shape_init(s);
    nmap[0] = 0;
    order[0] = 0;
    int nn = 1, nt = 0;
    for (int q = 0; q < nn; q++) {
        int A = order[q];
        if (g->pcount[A] == 0) die("nonterminal '%s' has no rule", g->ntname[A]);
        for (int x = 0; x < g->pcount[A]; x++) {
            if (x > 0) shape_push(s, A_BAR, 0);
            const Prod *pr = &g->prod[g->plist[A][x]];
            for (int j = 0; j < pr->len; j++) {
                int id = pr->rhs[j].id;
                if (pr->rhs[j].is_nt) {
                    if (nmap[id] < 0) { nmap[id] = nn; order[nn++] = id; }
                    shape_push(s, A_N, nmap[id]);
                } else {
                    if (tmap[id] < 0) { tmap[id] = nt; tord[nt++] = id; }
                    shape_push(s, A_T, tmap[id]);
                }
            }
        }
        shape_push(s, A_END, 0);
    }
    free(nmap);
    free(tmap);
    if (tfirst) *tfirst = tord; else free(tord);
    if (nfirst) *nfirst = order; else free(order);
    return g->nn - nn;
}

static void print_terminal(const char *name, FILE *f)
{
    char q = strchr(name, '\'') ? '"' : '\'';
    fputc(q, f);
    fputs(name, f);
    fputc(q, f);
}

static void print_nt(int i, const char *const *ntnames, int nntnames, FILE *f)
{
    if (i < nntnames) fputs(ntnames[i], f);
    else fprintf(f, "N%d", i);
}

void shape_print(const Shape *s, const char *const *ntnames, int nntnames,
                 const char *const *tnames, int oneline, FILE *f)
{
    int rule = 0, empty = 1;
    char buf[32];
    print_nt(0, ntnames, nntnames, f);
    fputs(" ->", f);
    for (int i = 0; i < s->n; i++) {
        const Action *a = &s->a[i];
        switch (a->kind) {
        case A_T:
            fputc(' ', f);
            if (tnames) print_terminal(tnames[a->id], f);
            else { snprintf(buf, sizeof buf, "t%d", a->id); print_terminal(buf, f); }
            empty = 0;
            break;
        case A_N:
            fputc(' ', f);
            print_nt(a->id, ntnames, nntnames, f);
            empty = 0;
            break;
        case A_BAR:
            if (empty) fputs(" eps", f);
            fputs(" |", f);
            empty = 1;
            break;
        case A_END:
            if (empty) fputs(" eps", f);
            empty = 1;
            rule++;
            if (i + 1 < s->n) {
                fputs(oneline ? " ; " : "\n", f);
                print_nt(rule, ntnames, nntnames, f);
                fputs(" ->", f);
            }
            break;
        }
    }
}

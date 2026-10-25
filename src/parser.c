#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "earley.h"
#include "parser.h"
#include "util.h"

void tokenize(const Grammar *g, const char *s, Tokens *out)
{
    int cap = 0;
    out->v = NULL;
    out->n = 0;
    size_t i = 0, n = strlen(s);
    while (i < n) {
        int best = -1;
        size_t bl = 0;
        for (int t = 0; t < g->nt; t++) {
            size_t l = strlen(g->tname[t]);
            if (l > bl && i + l <= n && !memcmp(s + i, g->tname[t], l)) { best = t; bl = l; }
        }
        if (best < 0 && isspace((unsigned char)s[i])) { i++; continue; }
        if (best < 0) die("cannot tokenize input at offset %zu: '%s'", i, s + i);
        if (out->n == cap) { cap = cap ? 2 * cap : 32; out->v = xrealloc(out->v, (size_t)cap * sizeof *out->v); }
        out->v[out->n++] = best;
        i += bl;
    }
}

void tokens_free(Tokens *t)
{
    free(t->v);
    t->v = NULL;
    t->n = 0;
}

typedef struct { const Grammar *g; const Tokens *tk; int pos; } Parser;

static int lookahead(const Parser *p)
{
    return p->pos < p->tk->n ? p->tk->v[p->pos] : p->g->nt;
}

static const char *token_name(const Grammar *g, int t)
{
    return t == g->nt ? "end of input" : g->tname[t];
}

static Node *parse_nt(Parser *p, int A)
{
    const Grammar *g = p->g;
    int look = lookahead(p);
    int prod = -1;
    for (int k = 0; k < g->pcount[A]; k++)
        if (g->pred[g->plist[A][k]][look]) { prod = g->plist[A][k]; break; }
    if (prod < 0)
        die("syntax error at token %d (%s): no rule for %s", p->pos + 1, token_name(g, look), g->ntname[A]);

    Node *n = node_new(g, A, prod);
    const Prod *pr = &g->prod[prod];
    int ki = 0;
    for (int k = 0; k < pr->len; k++) {
        if (pr->rhs[k].is_nt) { n->kids[ki++] = parse_nt(p, pr->rhs[k].id); continue; }
        int l = lookahead(p);
        if (l != pr->rhs[k].id)
            die("syntax error at token %d: expected '%s', found %s", p->pos + 1,
                g->tname[pr->rhs[k].id], token_name(g, l));
        p->pos++;
    }
    node_finish(n);
    return n;
}

Node *parse(const Grammar *g, const Tokens *tk)
{
    if (!g->ll1_ok) return earley_parse(g, tk);
    Parser p = { g, tk, 0 };
    Node *n = parse_nt(&p, 0);
    if (p.pos != tk->n)
        die("syntax error: unexpected '%s' after a complete string", g->tname[tk->v[p.pos]]);
    return n;
}

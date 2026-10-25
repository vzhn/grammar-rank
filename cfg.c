/*
 * cfg.c -- a bijection between the strings of an LL(1) language and the
 * natural numbers, graded by derivation size.
 *
 * The size of a string is the number of production applications in its
 * (unique) derivation tree.  Strings are ordered by size, then inside a size
 * class by: production index of the root (grammar order), then the split of
 * the remaining size among the nonterminal children, then the children left
 * to right.  This is a bijection because an LL(1) grammar is unambiguous.
 *
 * Grammar file:
 *     # comment
 *     E  -> T E1
 *     E1 -> '+' T E1 | eps
 *     F  -> '(' E ')' | 'x'
 * Quoted text is a terminal, a bare identifier is a nonterminal, `eps` or an
 * empty alternative is the empty string, `|` separates alternatives, `->` or
 * `::=` separates sides.  The first nonterminal is the start symbol.
 * Input strings are tokenized by longest match against the terminals;
 * whitespace between tokens is ignored.
 *
 * Usage:
 *   cfg check    G            FIRST/FOLLOW, LL(1) verdict, finiteness
 *   cfg rank     G 'string'   index of a string
 *   cfg unrank   G N [--tree] string with index N
 *   cfg list     G K [--tree] first K strings
 *   cfg count    G S          strings per size up to S
 *   cfg selftest G            brute force + round trips
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gmp.h>

/* ------------------------------------------------------------- utilities */

static void die(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
#include <stdarg.h>
static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("cfg: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) die("out of memory");
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p) die("out of memory");
    return p;
}

static char *xstrndup(const char *s, size_t n)
{
    char *r = xmalloc(n + 1);
    memcpy(r, s, n);
    r[n] = 0;
    return r;
}

/* --------------------------------------------------------------- grammar */

#define MAXRHS 64

typedef struct { int is_nt, id; } Ref;

typedef struct { mpz_t *v; int cap; } Tab; /* v[s] valid for s < built */

typedef struct {
    int lhs;
    int len;
    Ref rhs[MAXRHS];
    int m;              /* number of nonterminals in rhs */
    int ntpos[MAXRHS];  /* rhs positions of those nonterminals */
    Tab *suf;           /* suf[i](t): ways for nonterminals i.. to use t applications; m+1 tables */
    unsigned char *pred;/* predict set over terminals + END */
} Prod;

static char **ntname; static int NN;   /* nonterminals, 0 = start */
static char **tname;  static int NT;   /* terminals; END == NT */
static Prod *prod;    static int NP;
static int **plist;   static int *pcount; /* productions of each nonterminal, grammar order */

static unsigned char *nullable;
static unsigned char **first;   /* [NN][NT]   */
static unsigned char **follow;  /* [NN][NT+1] */
static unsigned char *productive, *reachable;
static int ll1_ok = 1;
static int finite_lang = 0;
static int max_size = 0;        /* for finite languages: largest derivation size */

static int find_name(char **names, int n, const char *s, size_t len)
{
    for (int i = 0; i < n; i++)
        if (strlen(names[i]) == len && !memcmp(names[i], s, len)) return i;
    return -1;
}

static int add_terminal(const char *s, size_t len)
{
    int id = find_name(tname, NT, s, len);
    if (id >= 0) return id;
    tname = xrealloc(tname, (size_t)(NT + 1) * sizeof *tname);
    tname[NT] = xstrndup(s, len);
    return NT++;
}

/* grammar tokens */
enum { TK_IDENT, TK_LIT, TK_ARROW, TK_BAR, TK_EOF };
typedef struct { int kind; const char *s; size_t len; int line; } GTok;

static GTok *gtok; static int ngtok;

static void scan_grammar(const char *text)
{
    int cap = 0, line = 1;
    const char *p = text;
    for (;;) {
        while (*p) {
            if (*p == '\n') { line++; p++; }
            else if (isspace((unsigned char)*p)) p++;
            else if (*p == '#') { while (*p && *p != '\n') p++; }
            else break;
        }
        GTok t = { TK_EOF, p, 0, line };
        if (!*p) { /* end */ }
        else if (*p == '|') { t.kind = TK_BAR; t.len = 1; p++; }
        else if (p[0] == '-' && p[1] == '>') { t.kind = TK_ARROW; t.len = 2; p += 2; }
        else if (p[0] == ':' && p[1] == ':' && p[2] == '=') { t.kind = TK_ARROW; t.len = 3; p += 3; }
        else if (*p == '\'' || *p == '"') {
            char q = *p++;
            const char *s = p;
            while (*p && *p != q && *p != '\n') p++;
            if (*p != q) die("line %d: unterminated quote", line);
            if (p == s) die("line %d: empty terminal", line);
            t.kind = TK_LIT; t.s = s; t.len = (size_t)(p - s);
            p++;
        }
        else if (isalpha((unsigned char)*p) || *p == '_') {
            const char *s = p;
            while (isalnum((unsigned char)*p) || *p == '_') p++;
            t.kind = TK_IDENT; t.s = s; t.len = (size_t)(p - s);
        }
        else die("line %d: unexpected character '%c'", line, *p);

        if (ngtok == cap) { cap = cap ? 2 * cap : 64; gtok = xrealloc(gtok, (size_t)cap * sizeof *gtok); }
        gtok[ngtok++] = t;
        if (t.kind == TK_EOF) return;
    }
}

static int rule_starts_at(int i)
{
    return gtok[i].kind == TK_IDENT && gtok[i + 1].kind == TK_ARROW;
}

static void load_grammar(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) die("cannot open %s", path);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = xmalloc((size_t)n + 1);
    if (fread(text, 1, (size_t)n, f) != (size_t)n) die("cannot read %s", path);
    text[n] = 0;
    fclose(f);
    scan_grammar(text);

    /* pass 1: nonterminals are the identifiers on a left-hand side */
    for (int i = 0; i + 1 < ngtok; i++) {
        if (!rule_starts_at(i)) continue;
        if (find_name(ntname, NN, gtok[i].s, gtok[i].len) < 0) {
            ntname = xrealloc(ntname, (size_t)(NN + 1) * sizeof *ntname);
            ntname[NN++] = xstrndup(gtok[i].s, gtok[i].len);
        }
    }
    if (NN == 0) die("%s: no rules found", path);

    /* pass 2: productions */
    int i = 0;
    if (!rule_starts_at(0)) die("line %d: expected 'Name ->'", gtok[0].line);
    while (gtok[i].kind != TK_EOF) {
        int lhs = find_name(ntname, NN, gtok[i].s, gtok[i].len);
        i += 2;
        for (;;) {
            Prod pr;
            memset(&pr, 0, sizeof pr);
            pr.lhs = lhs;
            int saw_eps = 0;
            while (gtok[i].kind != TK_EOF && gtok[i].kind != TK_BAR && !rule_starts_at(i)) {
                GTok *t = &gtok[i++];
                if (t->kind == TK_ARROW) die("line %d: unexpected '->'", t->line);
                if (pr.len == MAXRHS) die("line %d: right-hand side too long", t->line);
                if (t->kind == TK_IDENT) {
                    if (t->len == 3 && !memcmp(t->s, "eps", 3)) { saw_eps = 1; continue; }
                    int id = find_name(ntname, NN, t->s, t->len);
                    if (id < 0) die("line %d: undefined nonterminal '%.*s' (quote it if it is a terminal)",
                                    t->line, (int)t->len, t->s);
                    pr.rhs[pr.len++] = (Ref){ 1, id };
                } else {
                    pr.rhs[pr.len++] = (Ref){ 0, add_terminal(t->s, t->len) };
                }
            }
            if (saw_eps && pr.len) die("line %d: 'eps' mixed with other symbols", gtok[i - 1].line);
            for (int k = 0; k < pr.len; k++)
                if (pr.rhs[k].is_nt) pr.ntpos[pr.m++] = k;
            prod = xrealloc(prod, (size_t)(NP + 1) * sizeof *prod);
            prod[NP++] = pr;
            if (gtok[i].kind == TK_BAR) { i++; continue; }
            break;
        }
    }

    plist = xmalloc((size_t)NN * sizeof *plist);
    pcount = xmalloc((size_t)NN * sizeof *pcount);
    for (int a = 0; a < NN; a++) {
        pcount[a] = 0;
        for (int p = 0; p < NP; p++) if (prod[p].lhs == a) pcount[a]++;
        plist[a] = xmalloc((size_t)pcount[a] * sizeof **plist);
        int k = 0;
        for (int p = 0; p < NP; p++) if (prod[p].lhs == a) plist[a][k++] = p;
    }
    free(text);
}

/* ------------------------------------------------------- grammar analysis */

static unsigned char **alloc_sets(int rows, int cols)
{
    unsigned char **s = xmalloc((size_t)rows * sizeof *s);
    for (int i = 0; i < rows; i++) s[i] = calloc((size_t)cols, 1);
    return s;
}

/* add FIRST of rhs[from..] to dst; returns 1 if that suffix is nullable */
static int first_of_suffix(unsigned char *dst, const Prod *p, int from)
{
    for (int k = from; k < p->len; k++) {
        Ref r = p->rhs[k];
        if (!r.is_nt) { dst[r.id] = 1; return 0; }
        for (int t = 0; t < NT; t++) dst[t] |= first[r.id][t];
        if (!nullable[r.id]) return 0;
    }
    return 1;
}

static void analyze(void)
{
    nullable = calloc((size_t)NN, 1);
    first = alloc_sets(NN, NT);
    follow = alloc_sets(NN, NT + 1);
    productive = calloc((size_t)NN, 1);
    reachable = calloc((size_t)NN, 1);

    for (int changed = 1; changed;) {
        changed = 0;
        for (int p = 0; p < NP; p++) {
            Prod *pr = &prod[p];
            int nul = 1, prodv = 1;
            for (int k = 0; k < pr->len; k++) {
                if (!pr->rhs[k].is_nt) { nul = 0; continue; }
                if (!nullable[pr->rhs[k].id]) nul = 0;
                if (!productive[pr->rhs[k].id]) prodv = 0;
            }
            if (nul && !nullable[pr->lhs]) { nullable[pr->lhs] = 1; changed = 1; }
            if (prodv && !productive[pr->lhs]) { productive[pr->lhs] = 1; changed = 1; }
        }
    }
    for (int changed = 1; changed;) {
        changed = 0;
        for (int p = 0; p < NP; p++) {
            unsigned char tmp[NT > 0 ? NT : 1];
            memset(tmp, 0, sizeof tmp);
            first_of_suffix(tmp, &prod[p], 0);
            for (int t = 0; t < NT; t++)
                if (tmp[t] && !first[prod[p].lhs][t]) { first[prod[p].lhs][t] = 1; changed = 1; }
        }
    }
    follow[0][NT] = 1;
    for (int changed = 1; changed;) {
        changed = 0;
        for (int p = 0; p < NP; p++) {
            Prod *pr = &prod[p];
            for (int k = 0; k < pr->len; k++) {
                if (!pr->rhs[k].is_nt) continue;
                int X = pr->rhs[k].id;
                unsigned char tmp[NT + 1];
                memset(tmp, 0, sizeof tmp);
                if (first_of_suffix(tmp, pr, k + 1))
                    for (int t = 0; t <= NT; t++) tmp[t] |= follow[pr->lhs][t];
                for (int t = 0; t <= NT; t++)
                    if (tmp[t] && !follow[X][t]) { follow[X][t] = 1; changed = 1; }
            }
        }
    }
    for (int p = 0; p < NP; p++) {
        Prod *pr = &prod[p];
        pr->pred = calloc((size_t)NT + 1, 1);
        if (first_of_suffix(pr->pred, pr, 0))
            for (int t = 0; t <= NT; t++) pr->pred[t] |= follow[pr->lhs][t];
    }

    /* reachability */
    reachable[0] = 1;
    for (int changed = 1; changed;) {
        changed = 0;
        for (int p = 0; p < NP; p++) {
            if (!reachable[prod[p].lhs]) continue;
            for (int k = 0; k < prod[p].len; k++)
                if (prod[p].rhs[k].is_nt && !reachable[prod[p].rhs[k].id]) {
                    reachable[prod[p].rhs[k].id] = 1; changed = 1;
                }
        }
    }

    /* LL(1): predict sets of the alternatives of one nonterminal are disjoint */
    for (int a = 0; a < NN; a++)
        for (int x = 0; x < pcount[a]; x++)
            for (int y = x + 1; y < pcount[a]; y++)
                for (int t = 0; t <= NT; t++)
                    if (prod[plist[a][x]].pred[t] && prod[plist[a][y]].pred[t]) {
                        if (ll1_ok) fprintf(stderr, "cfg: grammar is not LL(1):\n");
                        ll1_ok = 0;
                        fprintf(stderr, "  %s: alternatives %d and %d both predicted by %s\n",
                                ntname[a], x + 1, y + 1, t == NT ? "end of input" : tname[t]);
                        goto next_pair;
                    next_pair:;
                    }

    /* finiteness: is there a cycle among useful nonterminals via live productions? */
    {
        int *color = calloc((size_t)NN, sizeof *color);   /* 0 white 1 grey 2 black */
        int *stack = xmalloc((size_t)NN * sizeof *stack), *iter = xmalloc((size_t)NN * sizeof *iter);
        int cyclic = 0;
        for (int root = 0; root < NN && !cyclic; root++) {
            if (color[root] || !productive[root] || !reachable[root]) continue;
            int sp = 0;
            stack[sp] = root; iter[sp] = 0; color[root] = 1; sp++;
            while (sp && !cyclic) {
                int a = stack[sp - 1];
                /* enumerate nonterminal children of a across its live productions */
                int found = -1, idx = 0;
                for (int k = 0; k < pcount[a] && found < 0; k++) {
                    Prod *pr = &prod[plist[a][k]];
                    int live = 1;
                    for (int j = 0; j < pr->m; j++) if (!productive[pr->rhs[pr->ntpos[j]].id]) live = 0;
                    if (!live) continue;
                    for (int j = 0; j < pr->m; j++, idx++)
                        if (idx == iter[sp - 1]) { found = pr->rhs[pr->ntpos[j]].id; break; }
                }
                if (found < 0) { color[a] = 2; sp--; continue; }
                iter[sp - 1]++;
                if (color[found] == 1) cyclic = 1;
                else if (color[found] == 0) { color[found] = 1; stack[sp] = found; iter[sp] = 0; sp++; }
            }
        }
        finite_lang = !cyclic;
        if (finite_lang) {
            /* max derivation size, by memoized DP (grammar is acyclic on useful part) */
            int *ms = xmalloc((size_t)NN * sizeof *ms);
            for (int a = 0; a < NN; a++) ms[a] = -1;
            for (int changed = 1; changed;) {
                changed = 0;
                for (int a = 0; a < NN; a++) {
                    if (ms[a] >= 0 || !productive[a]) continue;
                    int best = -1, ok = 1;
                    for (int k = 0; k < pcount[a]; k++) {
                        Prod *pr = &prod[plist[a][k]];
                        int s = 1, live = 1;
                        for (int j = 0; j < pr->m; j++) {
                            int X = pr->rhs[pr->ntpos[j]].id;
                            if (!productive[X]) { live = 0; break; }
                            if (ms[X] < 0) { ok = 0; break; }
                            s += ms[X];
                        }
                        if (!live) continue;
                        if (!ok) break;
                        if (s > best) best = s;
                    }
                    if (ok && best >= 0) { ms[a] = best; changed = 1; }
                }
            }
            max_size = productive[0] ? ms[0] : 0;
            free(ms);
        }
        free(color); free(stack); free(iter);
    }
}

/* --------------------------------------------------------- counting table */

static int built = 0;   /* sizes 0..built-1 are computed */
static Tab *N;          /* N[A](s): trees rooted at A with s applications */
static mpz_t *off;      /* off[s]: closed count of start trees with size < s; cap built+1 */

static void tab_grow(Tab *t, int need)
{
    if (t->cap >= need) return;
    int ncap = t->cap ? t->cap : 16;
    while (ncap < need) ncap *= 2;
    t->v = xrealloc(t->v, (size_t)ncap * sizeof *t->v);
    for (int i = t->cap; i < ncap; i++) mpz_init(t->v[i]);
    t->cap = ncap;
}

static void ensure(int S)
{
    if (S < built) return;
    if (!N) {
        N = calloc((size_t)NN, sizeof *N);
        for (int p = 0; p < NP; p++) prod[p].suf = calloc((size_t)prod[p].m + 1, sizeof(Tab));
        off = xmalloc(sizeof *off);
        mpz_init_set_ui(off[0], 0);
    }
    off = xrealloc(off, (size_t)(S + 2) * sizeof *off);
    for (int s = built + 1; s <= S + 1; s++) mpz_init(off[s]);
    for (int a = 0; a < NN; a++) tab_grow(&N[a], S + 1);
    for (int p = 0; p < NP; p++)
        for (int i = 0; i <= prod[p].m; i++) tab_grow(&prod[p].suf[i], S + 1);

    mpz_t tmp;
    mpz_init(tmp);
    for (int s = built; s <= S; s++) {
        for (int a = 0; a < NN; a++) {
            mpz_set_ui(N[a].v[s], 0);
            if (s == 0) continue;
            for (int k = 0; k < pcount[a]; k++)
                mpz_add(N[a].v[s], N[a].v[s], prod[plist[a][k]].suf[0].v[s - 1]);
        }
        for (int p = 0; p < NP; p++) {
            Prod *pr = &prod[p];
            mpz_set_ui(pr->suf[pr->m].v[s], s == 0 ? 1 : 0);
            for (int i = pr->m - 1; i >= 0; i--) {
                int X = pr->rhs[pr->ntpos[i]].id;
                mpz_ptr v = pr->suf[i].v[s];
                mpz_set_ui(v, 0);
                for (int j = 1; j <= s; j++) {
                    mpz_mul(tmp, N[X].v[j], pr->suf[i + 1].v[s - j]);
                    mpz_add(v, v, tmp);
                }
            }
        }
        mpz_add(off[s + 1], off[s], N[0].v[s]);
    }
    mpz_clear(tmp);
    built = S + 1;
}

/* ------------------------------------------------------------------ trees */

typedef struct Node {
    int nt, prod, size;
    struct Node **kids;   /* prod[prod].m children, nonterminal positions only */
} Node;

static Node *new_node(int nt, int p)
{
    Node *n = xmalloc(sizeof *n);
    n->nt = nt; n->prod = p; n->size = 0;
    n->kids = calloc((size_t)prod[p].m + (prod[p].m == 0), sizeof *n->kids);
    return n;
}

static void finish(Node *n)
{
    n->size = 1;
    for (int i = 0; i < prod[n->prod].m; i++) n->size += n->kids[i]->size;
}

static void node_free(Node *n)
{
    if (!n) return;
    for (int i = 0; i < prod[n->prod].m; i++) node_free(n->kids[i]);
    free(n->kids);
    free(n);
}

static Node *node_copy(const Node *n)
{
    Node *c = new_node(n->nt, n->prod);
    for (int i = 0; i < prod[n->prod].m; i++) c->kids[i] = node_copy(n->kids[i]);
    c->size = n->size;
    return c;
}

static int node_eq(const Node *a, const Node *b)
{
    if (a->nt != b->nt || a->prod != b->prod) return 0;
    for (int i = 0; i < prod[a->prod].m; i++)
        if (!node_eq(a->kids[i], b->kids[i])) return 0;
    return 1;
}

/* leaves of the tree, as terminal ids */
static void leaves(const Node *n, int **out, int *len, int *cap)
{
    const Prod *pr = &prod[n->prod];
    int ki = 0;
    for (int k = 0; k < pr->len; k++) {
        if (pr->rhs[k].is_nt) { leaves(n->kids[ki++], out, len, cap); continue; }
        if (*len == *cap) { *cap = *cap ? 2 * *cap : 32; *out = xrealloc(*out, (size_t)*cap * sizeof **out); }
        (*out)[(*len)++] = pr->rhs[k].id;
    }
}

static void print_string(const Node *n, FILE *f)
{
    int *tok = NULL, len = 0, cap = 0;
    leaves(n, &tok, &len, &cap);
    for (int i = 0; i < len; i++) fprintf(f, i ? " %s" : "%s", tname[tok[i]]);
    free(tok);
}

static void print_tree(const Node *n, FILE *f)
{
    const Prod *pr = &prod[n->prod];
    fprintf(f, "[%s", ntname[n->nt]);
    if (pr->len == 0) fputs(" eps", f);
    int ki = 0;
    for (int k = 0; k < pr->len; k++) {
        fputc(' ', f);
        if (pr->rhs[k].is_nt) print_tree(n->kids[ki++], f);
        else fprintf(f, "'%s'", tname[pr->rhs[k].id]);
    }
    fputc(']', f);
}

/* ---------------------------------------------------------------- parsing */

static int *tokens; static int ntokens, tpos;

static void tokenize(const char *s)
{
    int cap = 0;
    ntokens = 0;
    size_t i = 0, n = strlen(s);
    while (i < n) {
        if (isspace((unsigned char)s[i])) { i++; continue; }
        int best = -1; size_t bl = 0;
        for (int t = 0; t < NT; t++) {
            size_t l = strlen(tname[t]);
            if (l > bl && i + l <= n && !memcmp(s + i, tname[t], l)) { best = t; bl = l; }
        }
        if (best < 0) die("cannot tokenize input at offset %zu: '%s'", i, s + i);
        if (ntokens == cap) { cap = cap ? 2 * cap : 32; tokens = xrealloc(tokens, (size_t)cap * sizeof *tokens); }
        tokens[ntokens++] = best;
        i += bl;
    }
}

static Node *parse_nt(int A)
{
    int look = tpos < ntokens ? tokens[tpos] : NT;
    int p = -1;
    for (int k = 0; k < pcount[A]; k++)
        if (prod[plist[A][k]].pred[look]) { p = plist[A][k]; break; }
    if (p < 0)
        die("syntax error at token %d (%s): no rule for %s", tpos + 1,
            look == NT ? "end of input" : tname[look], ntname[A]);
    Node *n = new_node(A, p);
    Prod *pr = &prod[p];
    int ki = 0;
    for (int k = 0; k < pr->len; k++) {
        if (pr->rhs[k].is_nt) { n->kids[ki++] = parse_nt(pr->rhs[k].id); continue; }
        int l = tpos < ntokens ? tokens[tpos] : NT;
        if (l != pr->rhs[k].id)
            die("syntax error at token %d: expected '%s', found %s", tpos + 1,
                tname[pr->rhs[k].id], l == NT ? "end of input" : tname[l]);
        tpos++;
    }
    finish(n);
    return n;
}

static Node *parse_tokens(void)
{
    tpos = 0;
    Node *n = parse_nt(0);
    if (tpos != ntokens) die("syntax error: unexpected '%s' after a complete string", tname[tokens[tpos]]);
    return n;
}

/* ---------------------------------------------------------------- ranking */

static void rank_in(mpz_t r, const Node *n);

/* position of children i.. of n among all ways to spend t applications on them */
static void rank_suf(mpz_t r, const Node *n, int i, int t)
{
    const Prod *p = &prod[n->prod];
    mpz_set_ui(r, 0);
    if (i == p->m) return;
    const Node *c = n->kids[i];
    int si = c->size, X = c->nt;
    mpz_t tmp;
    mpz_init(tmp);
    for (int j = 1; j < si; j++) {                       /* smaller first child */
        mpz_mul(tmp, N[X].v[j], p->suf[i + 1].v[t - j]);
        mpz_add(r, r, tmp);
    }
    rank_in(tmp, c);                                     /* same size, smaller child */
    mpz_mul(tmp, tmp, p->suf[i + 1].v[t - si]);
    mpz_add(r, r, tmp);
    rank_suf(tmp, n, i + 1, t - si);                     /* same child, rest */
    mpz_add(r, r, tmp);
    mpz_clear(tmp);
}

static void rank_in(mpz_t r, const Node *n)
{
    int t = n->size - 1;
    rank_suf(r, n, 0, t);
    for (int k = 0; k < pcount[n->nt]; k++) {            /* earlier productions */
        int q = plist[n->nt][k];
        if (q == n->prod) break;
        mpz_add(r, r, prod[q].suf[0].v[t]);
    }
}

static void rank(mpz_t r, const Node *n)
{
    ensure(n->size);
    rank_in(r, n);
    mpz_add(r, r, off[n->size]);
}

static Node *unrank_in(mpz_t r, int A, int s);

static void unrank_suf(mpz_t r, Node *n, int i, int t)
{
    Prod *p = &prod[n->prod];
    if (i == p->m) return;
    int X = p->rhs[p->ntpos[i]].id;
    mpz_t c, q;
    mpz_inits(c, q, NULL);
    for (int j = 1; j <= t; j++) {
        mpz_ptr rest = p->suf[i + 1].v[t - j];
        mpz_mul(c, N[X].v[j], rest);
        if (mpz_cmp(r, c) < 0) {
            mpz_fdiv_qr(q, r, r, rest);
            n->kids[i] = unrank_in(q, X, j);
            unrank_suf(r, n, i + 1, t - j);
            mpz_clears(c, q, NULL);
            return;
        }
        mpz_sub(r, r, c);
    }
    die("internal error: unrank_suf out of range");
}

static Node *unrank_in(mpz_t r, int A, int s)
{
    int t = s - 1;
    for (int k = 0; k < pcount[A]; k++) {
        int q = plist[A][k];
        if (mpz_cmp(r, prod[q].suf[0].v[t]) < 0) {
            Node *n = new_node(A, q);
            unrank_suf(r, n, 0, t);
            finish(n);
            return n;
        }
        mpz_sub(r, r, prod[q].suf[0].v[t]);
    }
    die("internal error: unrank_in out of range");
}

static Node *unrank(const mpz_t num)
{
    if (finite_lang) {
        ensure(max_size);
        if (mpz_cmp(num, off[max_size + 1]) >= 0) {
            gmp_fprintf(stderr, "cfg: the language is finite and has only %Zd strings\n", off[max_size + 1]);
            exit(1);
        }
    }
    int s = 0;
    ensure(0);
    while (mpz_cmp(off[s + 1], num) <= 0) { s++; ensure(s); }
    mpz_t r;
    mpz_init(r);
    mpz_sub(r, num, off[s]);
    Node *n = unrank_in(r, 0, s);
    mpz_clear(r);
    return n;
}

/* --------------------------------------------------------------- selftest */

typedef struct { void **v; size_t n, cap; } Vec;

static void vec_push(Vec *v, void *x)
{
    if (v->n == v->cap) { v->cap = v->cap ? 2 * v->cap : 16; v->v = xrealloc(v->v, v->cap * sizeof *v->v); }
    v->v[v->n++] = x;
}

/* brute-force enumeration, memoized per (nonterminal, size); the memo owns the trees */
static Vec **memo; static unsigned char **memo_done; static int *memo_cap;

static const Vec *gen(int A, int s);

/* all ways to fill kids i.. of production p with t applications; each result is a Node*[m] */
static void gen_suf(Vec *out, int p, int i, int t)
{
    Prod *pr = &prod[p];
    if (i == pr->m) {
        if (t == 0) vec_push(out, calloc((size_t)pr->m + 1, sizeof(Node *)));
        return;
    }
    int X = pr->rhs[pr->ntpos[i]].id;
    for (int j = 1; j <= t; j++) {
        const Vec *kids = gen(X, j);
        if (!kids->n) continue;
        Vec rests = {0};
        gen_suf(&rests, p, i + 1, t - j);
        for (size_t a = 0; a < kids->n; a++)
            for (size_t b = 0; b < rests.n; b++) {
                Node **arr = xmalloc((size_t)(pr->m + 1) * sizeof *arr);
                Node **rest = rests.v[b];
                memset(arr, 0, (size_t)(pr->m + 1) * sizeof *arr);
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

static const Vec *gen(int A, int s)
{
    static const Vec empty = {0};
    if (s <= 0) return &empty;
    if (!memo) {
        memo = calloc((size_t)NN, sizeof *memo);
        memo_done = calloc((size_t)NN, sizeof *memo_done);
        memo_cap = calloc((size_t)NN, sizeof *memo_cap);
    }
    if (s >= memo_cap[A]) {
        int ncap = memo_cap[A] ? memo_cap[A] : 16;
        while (ncap <= s) ncap *= 2;
        memo[A] = xrealloc(memo[A], (size_t)ncap * sizeof **memo);
        memo_done[A] = xrealloc(memo_done[A], (size_t)ncap);
        memset(memo[A] + memo_cap[A], 0, (size_t)(ncap - memo_cap[A]) * sizeof **memo);
        memset(memo_done[A] + memo_cap[A], 0, (size_t)(ncap - memo_cap[A]));
        memo_cap[A] = ncap;
    }
    Vec *out = &memo[A][s];
    if (memo_done[A][s]) return out;
    for (int k = 0; k < pcount[A]; k++) {
        int p = plist[A][k];
        Vec fills = {0};
        gen_suf(&fills, p, 0, s - 1);
        for (size_t f = 0; f < fills.n; f++) {
            Node *n = new_node(A, p);
            Node **arr = fills.v[f];
            for (int i = 0; i < prod[p].m; i++) n->kids[i] = arr[i];
            free(arr);
            finish(n);
            vec_push(out, n);
        }
        free(fills.v);
    }
    memo_done[A][s] = 1;
    return out;
}

static int failures;

static void fail(const char *what, const Node *n)
{
    failures++;
    fprintf(stderr, "FAIL: %s", what);
    if (n) { fputs(": ", stderr); print_tree(n, stderr); }
    fputc('\n', stderr);
}

static int selftest(void)
{
    mpz_t r, want;
    mpz_inits(r, want, NULL);
    unsigned long total = 0;

    for (int s = 0; s <= 60 && total < 5000 && !(finite_lang && s > max_size); s++) {
        const Vec *all = gen(0, s);
        ensure(s);
        if (mpz_cmp_ui(N[0].v[s], (unsigned long)all->n) != 0) fail("count disagrees with brute force", NULL);
        for (size_t i = 0; i < all->n; i++) {
            Node *t = all->v[i];
            rank(r, t);
            mpz_add_ui(want, off[s], (unsigned long)i);
            if (mpz_cmp(r, want) != 0) fail("rank differs from brute-force position", t);
            Node *u = unrank(want);
            if (!node_eq(u, t)) fail("unrank differs from brute-force tree", t);
            node_free(u);
            /* print, re-tokenize, re-parse */
            char *buf = NULL; size_t bl = 0;
            FILE *mf = open_memstream(&buf, &bl);
            print_string(t, mf);
            fclose(mf);
            tokenize(buf);
            Node *q = parse_tokens();
            if (!node_eq(q, t)) fail("string does not reparse to the same tree", t);
            node_free(q);
            free(buf);
        }
        printf("size %2d: %6zu strings ok\n", s, all->n);
        total += all->n;
    }

    unsigned long limit = 20000;
    if (finite_lang) {
        ensure(max_size);
        if (mpz_cmp_ui(off[max_size + 1], limit) < 0) limit = mpz_get_ui(off[max_size + 1]);
    }
    for (unsigned long i = 0; i < limit; i++) {
        mpz_set_ui(want, i);
        Node *t = unrank(want);
        rank(r, t);
        if (mpz_cmp(r, want) != 0) fail("rank(unrank(i)) != i", t);
        node_free(t);
    }
    printf("round trip 0..%lu ok\n", limit ? limit - 1 : 0);

    if (!finite_lang) {
        const char *big[] = { "1000000000000", "123456789012345678901234567890",
                              "99999999999999999999999999999999999999999999999999" };
        for (size_t i = 0; i < sizeof big / sizeof *big; i++) {
            mpz_set_str(want, big[i], 10);
            Node *t = unrank(want);
            rank(r, t);
            if (mpz_cmp(r, want) != 0) fail("big round trip", t);
            printf("%s -> size %d string ok\n", big[i], t->size);
            node_free(t);
        }
    }
    mpz_clears(r, want, NULL);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("selftest OK");
    return 0;
}

/* ------------------------------------------------------------------- main */

static void print_set(const unsigned char *set, int with_end)
{
    int any = 0;
    for (int t = 0; t < NT; t++) if (set[t]) { printf("%s'%s'", any ? " " : "", tname[t]); any = 1; }
    if (with_end && set[NT]) { printf("%s$", any ? " " : ""); any = 1; }
    if (!any) printf("-");
}

static int check(void)
{
    printf("start symbol: %s\n", ntname[0]);
    printf("terminals:   ");
    for (int t = 0; t < NT; t++) printf(" '%s'", tname[t]);
    printf("\n\n");
    for (int a = 0; a < NN; a++) {
        printf("%s:%s%s%s\n", ntname[a], nullable[a] ? " nullable" : "",
               productive[a] ? "" : " UNPRODUCTIVE", reachable[a] ? "" : " UNREACHABLE");
        printf("  first:  "); print_set(first[a], 0); putchar('\n');
        printf("  follow: "); print_set(follow[a], 1); putchar('\n');
        for (int k = 0; k < pcount[a]; k++) {
            Prod *pr = &prod[plist[a][k]];
            printf("  %d. %s ->", k + 1, ntname[a]);
            if (!pr->len) printf(" eps");
            for (int j = 0; j < pr->len; j++)
                printf(pr->rhs[j].is_nt ? " %s" : " '%s'", pr->rhs[j].is_nt ? ntname[pr->rhs[j].id] : tname[pr->rhs[j].id]);
            printf("   predict: "); print_set(pr->pred, 1); putchar('\n');
        }
    }
    printf("\nLL(1): %s\n", ll1_ok ? "yes" : "NO");
    if (finite_lang) {
        ensure(max_size);
        gmp_printf("language: finite, %Zd strings, largest derivation size %d\n", off[max_size + 1], max_size);
    } else {
        printf("language: infinite\n");
    }
    return ll1_ok ? 0 : 1;
}

static int usage(void)
{
    fputs("usage: cfg check G | rank G STRING | unrank G N [--tree] | list G K [--tree] | count G S | selftest G\n",
          stderr);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 3) return usage();
    const char *cmd = argv[1];
    int tree = argc > 3 && !strcmp(argv[argc - 1], "--tree");
    if (tree) argc--;

    load_grammar(argv[2]);
    analyze();
    if (!strcmp(cmd, "check")) return check();
    if (!ll1_ok) die("refusing to number strings of an ambiguous grammar");

    if (!strcmp(cmd, "rank") && argc == 4) {
        tokenize(argv[3]);
        Node *n = parse_tokens();
        mpz_t r;
        mpz_init(r);
        rank(r, n);
        if (tree) { print_tree(n, stdout); putchar('\n'); }
        mpz_out_str(stdout, 10, r);
        putchar('\n');
        return 0;
    }
    if (!strcmp(cmd, "unrank") && argc == 4) {
        mpz_t num;
        mpz_init(num);
        if (mpz_set_str(num, argv[3], 10) != 0 || mpz_sgn(num) < 0) die("not a natural number: %s", argv[3]);
        Node *n = unrank(num);
        if (tree) print_tree(n, stdout); else print_string(n, stdout);
        putchar('\n');
        return 0;
    }
    if (!strcmp(cmd, "list") && argc == 4) {
        long k = atol(argv[3]);
        mpz_t num;
        mpz_init(num);
        if (finite_lang) {
            ensure(max_size);
            if (mpz_cmp_si(off[max_size + 1], k) < 0) k = mpz_get_si(off[max_size + 1]);
        }
        for (long i = 0; i < k; i++) {
            mpz_set_si(num, i);
            Node *n = unrank(num);
            printf("%ld\t", i);
            if (tree) print_tree(n, stdout);
            else if (n->size && prod[n->prod].len == 0 && prod[n->prod].m == 0) fputs("ε", stdout);
            else print_string(n, stdout);
            putchar('\n');
            node_free(n);
        }
        return 0;
    }
    if (!strcmp(cmd, "count") && argc == 4) {
        int S = atoi(argv[3]);
        ensure(S);
        printf("size\tstrings\tfirst index\n");
        for (int s = 0; s <= S; s++)
            gmp_printf("%d\t%Zd\t%Zd\n", s, N[0].v[s], off[s]);
        return 0;
    }
    if (!strcmp(cmd, "selftest")) return selftest();
    return usage();
}

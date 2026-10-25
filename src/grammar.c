#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "grammar.h"
#include "util.h"

/* ---------------------------------------------------------- symbol tables */

static int find_name(char **names, int n, const char *s, size_t len)
{
    for (int i = 0; i < n; i++)
        if (strlen(names[i]) == len && !memcmp(names[i], s, len)) return i;
    return -1;
}

static int add_nt(Grammar *g, const char *s, size_t len)
{
    g->ntname = xrealloc(g->ntname, (size_t)(g->nn + 1) * sizeof *g->ntname);
    g->ntname[g->nn] = xstrndup(s, len);
    return g->nn++;
}

static int add_t(Grammar *g, const char *s, size_t len)
{
    int id = find_name(g->tname, g->nt, s, len);
    if (id >= 0) return id;
    g->tname = xrealloc(g->tname, (size_t)(g->nt + 1) * sizeof *g->tname);
    g->tname[g->nt] = xstrndup(s, len);
    return g->nt++;
}

static void add_prod(Grammar *g, Prod *pr)
{
    pr->m = 0;
    for (int k = 0; k < pr->len; k++) if (pr->rhs[k].is_nt) pr->ntpos[pr->m++] = k;
    g->prod = xrealloc(g->prod, (size_t)(g->np + 1) * sizeof *g->prod);
    g->prod[g->np++] = *pr;
}

static void add_eps(Grammar *g, int lhs)
{
    Prod pr;
    memset(&pr, 0, sizeof pr);
    pr.lhs = lhs;
    add_prod(g, &pr);
}

static void add_unary(Grammar *g, int lhs, Ref x, int tail)   /* lhs -> x [tail] */
{
    Prod pr;
    memset(&pr, 0, sizeof pr);
    pr.lhs = lhs;
    pr.rhs[pr.len++] = x;
    if (tail >= 0) pr.rhs[pr.len++] = (Ref){ 1, tail };
    add_prod(g, &pr);
}

/* ---------------------------------------------------------------- scanner */

enum { TK_IDENT, TK_LIT, TK_ARROW, TK_BAR, TK_LPAR, TK_RPAR, TK_LBRK, TK_RBRK,
       TK_LBRACE, TK_RBRACE, TK_STAR, TK_PLUS, TK_QMARK, TK_END, TK_EOF };

typedef struct { int kind; const char *s; size_t len; int line; } Tok;

typedef struct {
    Grammar *g;
    Tok *tok; int ntok, i;      /* token array and cursor */
    const char *cur_rule;       /* rule being read; base for generated names */
    int gen_count[4];
} Loader;

static void scan(Loader *L, const char *text)
{
    static const char punct[] = "()[]{}*+?";
    static const int punct_kind[] = { TK_LPAR, TK_RPAR, TK_LBRK, TK_RBRK, TK_LBRACE, TK_RBRACE,
                                      TK_STAR, TK_PLUS, TK_QMARK };
    int cap = 0, line = 1;
    const char *p = text;
    for (;;) {
        while (*p) {
            if (*p == '\n') { line++; p++; }
            else if (isspace((unsigned char)*p)) p++;
            else if (*p == '#') { while (*p && *p != '\n') p++; }
            else break;
        }
        Tok t = { TK_EOF, p, 0, line };
        if (!*p) { /* end */ }
        else if (*p == '|') { t.kind = TK_BAR; t.len = 1; p++; }
        else if (p[0] == '-' && p[1] == '>') { t.kind = TK_ARROW; t.len = 2; p += 2; }
        else if (p[0] == ':' && p[1] == ':' && p[2] == '=') { t.kind = TK_ARROW; t.len = 3; p += 3; }
        else if (*p == '=') { t.kind = TK_ARROW; t.len = 1; p++; }
        else if (*p == '.' || *p == ';') { t.kind = TK_END; t.len = 1; p++; }
        else if (strchr(punct, *p)) { t.kind = punct_kind[strchr(punct, *p) - punct]; t.len = 1; p++; }
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

        if (L->ntok == cap) { cap = cap ? 2 * cap : 64; L->tok = xrealloc(L->tok, (size_t)cap * sizeof *L->tok); }
        L->tok[L->ntok++] = t;
        if (t.kind == TK_EOF) return;
    }
}

static int rule_starts_at(const Loader *L, int i)
{
    return L->tok[i].kind == TK_IDENT && L->tok[i + 1].kind == TK_ARROW;
}

static void tok_error(const Tok *t, const char *what)
{
    if (t->kind == TK_EOF) die("line %d: %s, found end of input", t->line, what);
    die("line %d: %s, found '%.*s'", t->line, what, (int)t->len, t->s);
}

/* ------------------------------------------------- right-hand side parser */
/*
 * EBNF is desugared while reading.  Generated nonterminals are named after
 * the rule they occur in:  X.gN group, X.oN option, X.rN repetition, X.pN plus.
 *   ( a | b )      X.g -> a | b
 *   [ a ] , a?     X.o -> eps | a
 *   { a } , a*     X.r -> eps | a X.r
 *   a+             X.p -> a X.r        with X.r as above
 * The empty alternative always comes first, so at equal size fewer
 * iterations precede more.
 */

static int fresh(Loader *L, char kind)
{
    static const char kinds[] = "gorp";
    int *c = &L->gen_count[strchr(kinds, kind) - kinds];
    char buf[256];
    snprintf(buf, sizeof buf, "%s.%c%d", L->cur_rule, kind, ++*c);
    return add_nt(L->g, buf, strlen(buf));
}

static void parse_alts(Loader *L, int lhs, int tail);

static int at_seq_end(const Loader *L)
{
    int k = L->tok[L->i].kind;
    return k == TK_EOF || k == TK_END || k == TK_BAR || k == TK_RPAR || k == TK_RBRK || k == TK_RBRACE
        || rule_starts_at(L, L->i);
}

static void expect(Loader *L, int kind, const char *what)
{
    if (L->tok[L->i].kind != kind) tok_error(&L->tok[L->i], what);
    L->i++;
}

static Ref parse_atom(Loader *L)
{
    Grammar *g = L->g;
    Tok *t = &L->tok[L->i++];
    switch (t->kind) {
    case TK_LIT:
        return (Ref){ 0, add_t(g, t->s, t->len) };
    case TK_IDENT: {
        int id = find_name(g->ntname, g->nn, t->s, t->len);
        if (id < 0) die("line %d: undefined nonterminal '%.*s' (quote it if it is a terminal)",
                        t->line, (int)t->len, t->s);
        return (Ref){ 1, id };
    }
    case TK_LPAR: {
        int grp = fresh(L, 'g');
        parse_alts(L, grp, -1);
        expect(L, TK_RPAR, "expected ')'");
        return (Ref){ 1, grp };
    }
    case TK_LBRK: {
        int o = fresh(L, 'o');
        add_eps(g, o);
        parse_alts(L, o, -1);
        expect(L, TK_RBRK, "expected ']'");
        return (Ref){ 1, o };
    }
    case TK_LBRACE: {
        int r = fresh(L, 'r');
        add_eps(g, r);
        parse_alts(L, r, r);
        expect(L, TK_RBRACE, "expected '}'");
        return (Ref){ 1, r };
    }
    default:
        L->i--;
        tok_error(t, "expected a symbol or group");
    }
    return (Ref){ 0, 0 };
}

static Ref parse_factor(Loader *L)
{
    Grammar *g = L->g;
    Ref x = parse_atom(L);
    int k = L->tok[L->i].kind;
    if (k == TK_QMARK) {
        L->i++;
        int o = fresh(L, 'o');
        add_eps(g, o);
        add_unary(g, o, x, -1);
        return (Ref){ 1, o };
    }
    if (k == TK_STAR || k == TK_PLUS) {
        L->i++;
        int r = fresh(L, 'r');
        add_eps(g, r);
        add_unary(g, r, x, r);
        if (k == TK_STAR) return (Ref){ 1, r };
        int p = fresh(L, 'p');
        add_unary(g, p, x, r);
        return (Ref){ 1, p };
    }
    return x;
}

static void parse_seq(Loader *L, int lhs, int tail)
{
    Prod pr;
    memset(&pr, 0, sizeof pr);
    pr.lhs = lhs;
    int saw_eps = 0;
    while (!at_seq_end(L)) {
        Tok *t = &L->tok[L->i];
        if (t->kind == TK_IDENT && t->len == 3 && !memcmp(t->s, "eps", 3)) { saw_eps = 1; L->i++; continue; }
        if (pr.len + (tail >= 0) >= MAXRHS) die("line %d: right-hand side too long", t->line);
        pr.rhs[pr.len++] = parse_factor(L);
    }
    if (saw_eps && pr.len) die("line %d: 'eps' mixed with other symbols", L->tok[L->i - 1].line);
    if (tail >= 0) pr.rhs[pr.len++] = (Ref){ 1, tail };
    add_prod(L->g, &pr);
}

static void parse_alts(Loader *L, int lhs, int tail)
{
    for (;;) {
        parse_seq(L, lhs, tail);
        if (L->tok[L->i].kind != TK_BAR) return;
        L->i++;
    }
}

/* ----------------------------------------------------------------- loader */

static char *read_file(const char *path)
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
    return text;
}

void grammar_load(Grammar *g, const char *path)
{
    memset(g, 0, sizeof *g);
    Loader L;
    memset(&L, 0, sizeof L);
    L.g = g;

    char *text = read_file(path);
    scan(&L, text);

    /* pass 1: nonterminals are the identifiers on a left-hand side */
    for (int i = 0; i + 1 < L.ntok; i++) {
        if (!rule_starts_at(&L, i)) continue;
        if (find_name(g->ntname, g->nn, L.tok[i].s, L.tok[i].len) < 0)
            add_nt(g, L.tok[i].s, L.tok[i].len);
    }
    if (g->nn == 0) die("%s: no rules found", path);

    /* pass 2: productions, desugaring EBNF on the way */
    L.i = 0;
    while (L.tok[L.i].kind != TK_EOF) {
        if (!rule_starts_at(&L, L.i)) tok_error(&L.tok[L.i], "expected 'Name ->'");
        int lhs = find_name(g->ntname, g->nn, L.tok[L.i].s, L.tok[L.i].len);
        L.cur_rule = g->ntname[lhs];
        memset(L.gen_count, 0, sizeof L.gen_count);
        L.i += 2;
        parse_alts(&L, lhs, -1);
        if (L.tok[L.i].kind == TK_END) L.i++;   /* optional ISO-style terminator */
    }

    g->plist = xmalloc((size_t)g->nn * sizeof *g->plist);
    g->pcount = xmalloc((size_t)g->nn * sizeof *g->pcount);
    for (int a = 0; a < g->nn; a++) {
        g->pcount[a] = 0;
        for (int p = 0; p < g->np; p++) if (g->prod[p].lhs == a) g->pcount[a]++;
        g->plist[a] = xmalloc((size_t)g->pcount[a] * sizeof **g->plist);
        int k = 0;
        for (int p = 0; p < g->np; p++) if (g->prod[p].lhs == a) g->plist[a][k++] = p;
    }
    free(L.tok);
    free(text);
}

int grammar_is_generated(const Grammar *g, int nt)
{
    return strchr(g->ntname[nt], '.') != NULL;
}

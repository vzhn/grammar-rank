#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "grammar.h"
#include "util.h"

/* ---------------------------------------------------------- symbol tables */

/* lookup key: the name with all whitespace removed (ISO 14977 lets gap
 * separators appear inside a meta-identifier without changing it) */
static char *make_key(const char *s, size_t len)
{
    char *k = xmalloc(len + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i++)
        if (!isspace((unsigned char)s[i])) k[j++] = s[i];
    k[j] = 0;
    return k;
}

/* display form: whitespace runs collapsed to one space, trimmed */
static char *make_display(const char *s, size_t len)
{
    char *d = xmalloc(len + 1);
    size_t j = 0;
    int space = 1;
    for (size_t i = 0; i < len; i++) {
        if (isspace((unsigned char)s[i])) { if (!space) { d[j++] = ' '; space = 1; } }
        else { d[j++] = s[i]; space = 0; }
    }
    while (j && d[j - 1] == ' ') j--;
    d[j] = 0;
    return d;
}

static int find_nt_key(const Grammar *g, const char *key)
{
    for (int i = 0; i < g->nn; i++)
        if (!strcmp(g->ntkey[i], key)) return i;
    return -1;
}

static int add_nt(Grammar *g, const char *display, const char *key, int generated)
{
    g->ntname = xrealloc(g->ntname, (size_t)(g->nn + 1) * sizeof *g->ntname);
    g->ntkey = xrealloc(g->ntkey, (size_t)(g->nn + 1) * sizeof *g->ntkey);
    g->generated = xrealloc(g->generated, (size_t)(g->nn + 1));
    g->in_exception = xrealloc(g->in_exception, (size_t)(g->nn + 1));
    g->in_exception[g->nn] = 0;
    g->ntname[g->nn] = xstrndup(display, strlen(display));
    g->ntkey[g->nn] = xstrndup(key, strlen(key));
    g->generated[g->nn] = (unsigned char)generated;
    return g->nn++;
}

static int add_t(Grammar *g, const char *s, size_t len)
{
    for (int i = 0; i < g->nt; i++)
        if (strlen(g->tname[i]) == len && !memcmp(g->tname[i], s, len)) return i;
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

enum { TK_IDENT, TK_LIT, TK_INT, TK_ARROW, TK_BAR, TK_COMMA, TK_EXCEPT,
       TK_LPAR, TK_RPAR, TK_LBRK, TK_RBRK, TK_LBRACE, TK_RBRACE,
       TK_STAR, TK_PLUS, TK_QMARK, TK_END, TK_EOF };

typedef struct { int kind; const char *s; size_t len; int line; long value; } Tok;

typedef struct { int result; Ref factor, except; int line; } Pending;   /* an exception to resolve */

/* a byte-per-nonterminal flag array that grows with the grammar */
typedef struct { unsigned char *v; int n; } Flags;

static unsigned char *flag(Flags *f, int i)
{
    if (i >= f->n) {
        int n = f->n ? f->n : 64;
        while (n <= i) n *= 2;
        f->v = xrealloc(f->v, (size_t)n);
        memset(f->v + f->n, 0, (size_t)(n - f->n));
        f->n = n;
    }
    return &f->v[i];
}

typedef struct {
    Grammar *g;
    Tok *tok; int ntok, i;      /* token array and cursor */
    const char *cur_rule;       /* rule being read; base for generated names */
    Pending *pend; int npend;
    Flags resolving;            /* exception results being resolved (cycle check) */
} Loader;

static void skip_space_and_comments(const char **pp, int *line)
{
    const char *p = *pp;
    for (;;) {
        if (*p == '\n') { (*line)++; p++; }
        else if (isspace((unsigned char)*p)) p++;
        else if (*p == '#') { while (*p && *p != '\n') p++; }
        else if (p[0] == '(' && p[1] == '*') {
            int start = *line, depth = 1;
            p += 2;
            while (*p && depth) {
                if (p[0] == '(' && p[1] == '*') { depth++; p += 2; }
                else if (p[0] == '*' && p[1] == ')') { depth--; p += 2; }
                else { if (*p == '\n') (*line)++; p++; }
            }
            if (depth) die("line %d: unterminated comment", start);
        }
        else break;
    }
    *pp = p;
}

static void scan(Loader *L, const char *text)
{
    int cap = 0, line = 1;
    const char *p = text;
    for (;;) {
        skip_space_and_comments(&p, &line);
        Tok t = { TK_EOF, p, 0, line, 0 };
        unsigned char c = (unsigned char)*p;
        if (!c) { /* end */ }
        else if (c == '(' && p[1] == '/') { t.kind = TK_LBRK; t.len = 2; }
        else if (c == '(' && p[1] == ':') { t.kind = TK_LBRACE; t.len = 2; }
        else if (c == '/' && p[1] == ')') { t.kind = TK_RBRK; t.len = 2; }
        else if (c == ':' && p[1] == ')') { t.kind = TK_RBRACE; t.len = 2; }
        else if (c == '*' && p[1] == ')') die("line %d: unexpected '*)'", line);
        else if (c == '-' && p[1] == '>') { t.kind = TK_ARROW; t.len = 2; }
        else if (c == ':' && p[1] == ':' && p[2] == '=') { t.kind = TK_ARROW; t.len = 3; }
        else if (c == '=') { t.kind = TK_ARROW; t.len = 1; }
        else if (c == '|' || c == '/' || c == '!') { t.kind = TK_BAR; t.len = 1; }
        else if (c == ',') { t.kind = TK_COMMA; t.len = 1; }
        else if (c == '-') { t.kind = TK_EXCEPT; t.len = 1; }
        else if (c == '.' || c == ';') { t.kind = TK_END; t.len = 1; }
        else if (c == '(') { t.kind = TK_LPAR; t.len = 1; }
        else if (c == ')') { t.kind = TK_RPAR; t.len = 1; }
        else if (c == '[') { t.kind = TK_LBRK; t.len = 1; }
        else if (c == ']') { t.kind = TK_RBRK; t.len = 1; }
        else if (c == '{') { t.kind = TK_LBRACE; t.len = 1; }
        else if (c == '}') { t.kind = TK_RBRACE; t.len = 1; }
        else if (c == '*') { t.kind = TK_STAR; t.len = 1; }
        else if (c == '+') { t.kind = TK_PLUS; t.len = 1; }
        else if (c == '?') { t.kind = TK_QMARK; t.len = 1; }
        else if (c == '\'' || c == '"') {
            const char *s = p + 1, *e = s;
            while (*e && *e != (char)c && *e != '\n') e++;
            if (*e != (char)c) die("line %d: unterminated quote", line);
            if (e == s) die("line %d: empty terminal", line);
            t.kind = TK_LIT; t.s = s; t.len = (size_t)(e - s);
            p = e + 1;
        }
        else if (isdigit(c)) {
            const char *s = p;
            long v = 0;
            while (isdigit((unsigned char)*p)) { v = v * 10 + (*p - '0'); if (v > 100000) die("line %d: integer too large", line); p++; }
            t.kind = TK_INT; t.len = (size_t)(p - s); t.value = v;
        }
        else if (isalpha(c) || c == '_') {
            const char *s = p;
            while (isalnum((unsigned char)*p) || *p == '_') p++;
            t.kind = TK_IDENT; t.len = (size_t)(p - s);
        }
        else die("line %d: unexpected character '%c'", line, c);

        if (t.kind != TK_LIT && t.kind != TK_INT && t.kind != TK_IDENT) p += t.len;
        if (L->ntok == cap) { cap = cap ? 2 * cap : 64; L->tok = xrealloc(L->tok, (size_t)cap * sizeof *L->tok); }
        L->tok[L->ntok++] = t;
        if (t.kind == TK_EOF) return;
    }
}

static void tok_error(const Tok *t, const char *what)
{
    if (t->kind == TK_EOF) die("line %d: %s, found end of input", t->line, what);
    die("line %d: %s, found '%.*s'", t->line, what, (int)t->len, t->s);
}

/* ------------------------------------------------------------ rule starts */
/*
 * A left-hand side is one identifier followed by an arrow, or, when the rule
 * follows a terminator or starts the file, a run of identifiers followed by
 * an arrow (an ISO meta-identifier with spaces).  Returns the index of the
 * arrow, or -1 if no rule starts at i.
 */
static int lhs_arrow(const Loader *L, int i)
{
    if (L->tok[i].kind != TK_IDENT) return -1;
    if (i == 0 || L->tok[i - 1].kind == TK_END) {
        int j = i;
        while (L->tok[j].kind == TK_IDENT) j++;
        return L->tok[j].kind == TK_ARROW ? j : -1;
    }
    return L->tok[i + 1].kind == TK_ARROW ? i + 1 : -1;
}

static int rule_starts_at(const Loader *L, int i)
{
    return lhs_arrow(L, i) >= 0;
}

/* the text spanned by tokens [from, to) */
static void span(const Loader *L, int from, int to, const char **s, size_t *len)
{
    const Tok *a = &L->tok[from], *b = &L->tok[to - 1];
    *s = a->s;
    *len = (size_t)(b->s + b->len - a->s);
}

/* ------------------------------------------------- right-hand side parser */
/*
 * EBNF is desugared while reading.  Generated nonterminals are named after
 * the rule they occur in:  X.gN group, X.oN option, X.rN repetition, X.pN plus,
 * X.xN exception, X.nN integer repetition (only when an exception follows it).
 *   ( a | b )      X.g -> a | b
 *   [ a ] , a?     X.o -> eps | a
 *   { a } , a*     X.r -> eps | a X.r
 *   a+             X.p -> a X.r        with X.r as above
 *   3 * a          a a a
 *   a - b          X.x -> ...          see resolve_exception()
 * The empty alternative always comes first, so at equal size fewer
 * iterations precede more.
 */

static int fresh(Loader *L, char kind)
{
    char buf[512];
    for (int n = 1;; n++) {
        snprintf(buf, sizeof buf, "%s.%c%d", L->cur_rule, kind, n);
        char *key = make_key(buf, strlen(buf));
        int exists = find_nt_key(L->g, key) >= 0;
        free(key);
        if (!exists) break;
    }
    char *key = make_key(buf, strlen(buf));
    int id = add_nt(L->g, buf, key, 1);
    free(key);
    return id;
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

static int is_eps_word(const Loader *L)
{
    const Tok *t = &L->tok[L->i];
    return t->kind == TK_IDENT && t->len == 3 && !memcmp(t->s, "eps", 3) && find_nt_key(L->g, "eps") < 0;
}

/* a meta-identifier: the longest run of identifier words whose concatenation
 * names a defined nonterminal */
static Ref parse_name(Loader *L)
{
    int i = L->i, best = -1, bestk = 0;
    char *key = xstrndup("", 0);
    for (int k = 0; L->tok[i + k].kind == TK_IDENT; k++) {
        if (k > 0 && rule_starts_at(L, i + k)) break;
        const Tok *t = &L->tok[i + k];
        size_t kl = strlen(key);
        key = xrealloc(key, kl + t->len + 1);
        memcpy(key + kl, t->s, t->len);
        key[kl + t->len] = 0;
        int id = find_nt_key(L->g, key);
        if (id >= 0) { best = id; bestk = k + 1; }
    }
    free(key);
    if (best < 0) {
        const Tok *t = &L->tok[i];
        die("line %d: undefined nonterminal '%.*s' (quote it if it is a terminal)", t->line, (int)t->len, t->s);
    }
    L->i += bestk;
    return (Ref){ 1, best };
}

static Ref parse_atom(Loader *L)
{
    Grammar *g = L->g;
    Tok *t = &L->tok[L->i];
    switch (t->kind) {
    case TK_LIT:
        L->i++;
        return (Ref){ 0, add_t(g, t->s, t->len) };
    case TK_IDENT:
        return parse_name(L);
    case TK_LPAR: {
        L->i++;
        int grp = fresh(L, 'g');
        parse_alts(L, grp, -1);
        expect(L, TK_RPAR, "expected ')'");
        return (Ref){ 1, grp };
    }
    case TK_LBRK: {
        L->i++;
        int o = fresh(L, 'o');
        add_eps(g, o);
        parse_alts(L, o, -1);
        expect(L, TK_RBRK, "expected ']'");
        return (Ref){ 1, o };
    }
    case TK_LBRACE: {
        L->i++;
        int r = fresh(L, 'r');
        add_eps(g, r);
        parse_alts(L, r, r);
        expect(L, TK_RBRACE, "expected '}'");
        return (Ref){ 1, r };
    }
    case TK_QMARK:
        die("line %d: special sequences '? ... ?' are not supported", t->line);
    default:
        tok_error(t, "expected a symbol or group");
    }
    return (Ref){ 0, 0 };
}

/* primary with optional postfix operator */
static Ref parse_primary(Loader *L)
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

/* [integer '*'] primary; *count receives the repetition count */
static Ref parse_factor(Loader *L, int *count)
{
    *count = 1;
    if (L->tok[L->i].kind == TK_INT) {
        long n = L->tok[L->i].value;
        if (n > MAXRHS) die("line %d: repetition count %ld is too large", L->tok[L->i].line, n);
        L->i++;
        expect(L, TK_STAR, "expected '*' after integer");
        *count = (int)n;
    }
    return parse_primary(L);
}

/* a factor repeated count times, as a single symbol */
static Ref repeated(Loader *L, Ref x, int count)
{
    if (count == 1) return x;
    int n = fresh(L, 'n');
    Prod pr;
    memset(&pr, 0, sizeof pr);
    pr.lhs = n;
    for (int k = 0; k < count; k++) pr.rhs[pr.len++] = x;
    add_prod(L->g, &pr);
    return (Ref){ 1, n };
}

/* the right operand of '-': a factor, or nothing (the empty sequence) */
static Ref parse_exception(Loader *L)
{
    int k = L->tok[L->i].kind;
    if (at_seq_end(L) || k == TK_COMMA) {
        int e = fresh(L, 'e');
        add_eps(L->g, e);
        return (Ref){ 1, e };
    }
    int count;
    Ref x = parse_factor(L, &count);
    return repeated(L, x, count);
}

/* term = factor ['-' exception]; appends its symbols to pr */
static void parse_term(Loader *L, Prod *pr, int tail)
{
    int line = L->tok[L->i].line;
    int count;
    Ref x = parse_factor(L, &count);
    if (L->tok[L->i].kind == TK_EXCEPT) {
        L->i++;
        Ref f = repeated(L, x, count);
        Ref e = parse_exception(L);
        if (f.is_nt) L->g->in_exception[f.id] = 1;
        if (e.is_nt) L->g->in_exception[e.id] = 1;
        int r = fresh(L, 'x');
        L->pend = xrealloc(L->pend, (size_t)(L->npend + 1) * sizeof *L->pend);
        L->pend[L->npend++] = (Pending){ r, f, e, line };
        x = (Ref){ 1, r };
        count = 1;
    }
    if (pr->len + count + (tail >= 0) > MAXRHS) die("line %d: right-hand side too long", line);
    for (int k = 0; k < count; k++) pr->rhs[pr->len++] = x;
}

static void parse_seq(Loader *L, int lhs, int tail)
{
    Prod pr;
    memset(&pr, 0, sizeof pr);
    pr.lhs = lhs;
    int saw_eps = 0;
    while (!at_seq_end(L)) {
        Tok *t = &L->tok[L->i];
        if (t->kind == TK_COMMA) {
            L->i++;
            if (at_seq_end(L)) tok_error(&L->tok[L->i], "expected a symbol after ','");
            continue;
        }
        if (is_eps_word(L)) { saw_eps = 1; L->i++; continue; }
        parse_term(L, &pr, tail);
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

/* ------------------------------------------------------------- exceptions */
/*
 * factor - exception denotes L(factor) \ L(exception).  The standard requires
 * the exception to be expressible without meta-identifiers, i.e. regular, so
 * we build a finite automaton for it, complement it, and intersect the
 * factor's sub-grammar with that automaton (the Bar-Hillel product): every
 * nonterminal B of the sub-grammar becomes B[p,q], "a derivation of B whose
 * yield takes the automaton from state p to state q".  The product grammar
 * is unambiguous whenever the original is, so counting and ranking carry
 * over unchanged.
 */

static void resolve_nt(Loader *L, int A);

/* --- NFA with epsilon moves --- */

typedef struct { int from, to, sym; } Edge;   /* sym -1: epsilon */
typedef struct { Edge *e; int ne, cap, nstates; } NFA;

static int nfa_state(NFA *a) { return a->nstates++; }

static void nfa_edge(NFA *a, int from, int to, int sym)
{
    if (a->ne == a->cap) { a->cap = a->cap ? 2 * a->cap : 32; a->e = xrealloc(a->e, (size_t)a->cap * sizeof *a->e); }
    a->e[a->ne++] = (Edge){ from, to, sym };
}

typedef struct { Loader *L; Flags visiting; int line; } NfaCtx;

static void nfa_nt(NfaCtx *c, NFA *a, int A, int s, int t);

static void nfa_sym(NfaCtx *c, NFA *a, Ref r, int s, int t)
{
    if (!r.is_nt) nfa_edge(a, s, t, r.id);
    else nfa_nt(c, a, r.id, s, t);
}

/* rhs[from..to) between states s and t */
static void nfa_seq(NfaCtx *c, NFA *a, const Ref *rhs, int from, int to, int s, int t)
{
    if (from == to) { nfa_edge(a, s, t, -1); return; }
    int cur = s;
    for (int k = from; k < to; k++) {
        int nxt = k == to - 1 ? t : nfa_state(a);
        nfa_sym(c, a, rhs[k], cur, nxt);
        cur = nxt;
    }
}

/* A nonterminal is regular for our purposes if its only recursion is tail
 * recursion:  A -> a1 | ... | ak | b1 A | ... | bm A, so L(A) = (b1|..|bm)* (a1|..|ak). */
static void nfa_nt(NfaCtx *c, NFA *a, int A, int s, int t)
{
    Grammar *g = c->L->g;
    resolve_nt(c->L, A);
    if (*flag(&c->visiting, A))
        die("line %d: exception is not regular: '%s' is recursive", c->line, g->ntname[A]);
    *flag(&c->visiting, A) = 1;
    int q = nfa_state(a);
    nfa_edge(a, s, q, -1);
    for (int p = 0; p < g->np; p++) {
        const Prod *pr = &g->prod[p];
        if (pr->lhs != A) continue;
        int tail = pr->len > 0 && pr->rhs[pr->len - 1].is_nt && pr->rhs[pr->len - 1].id == A;
        if (tail) nfa_seq(c, a, pr->rhs, 0, pr->len - 1, q, q);
        else nfa_seq(c, a, pr->rhs, 0, pr->len, q, t);
    }
    *flag(&c->visiting, A) = 0;
}

/* --- DFA by subset construction, complete over all terminals --- */

typedef struct {
    int nstates, start;
    int *delta;             /* [nstates][nt] */
    unsigned char *accept;  /* [nstates] */
} DFA;

typedef struct { int *v; int n; } Set;

static void set_add(Set *s, int x)
{
    int lo = 0, hi = s->n;
    while (lo < hi) { int mid = (lo + hi) / 2; if (s->v[mid] < x) lo = mid + 1; else hi = mid; }
    if (lo < s->n && s->v[lo] == x) return;
    s->v = xrealloc(s->v, (size_t)(s->n + 1) * sizeof *s->v);
    memmove(s->v + lo + 1, s->v + lo, (size_t)(s->n - lo) * sizeof *s->v);
    s->v[lo] = x;
    s->n++;
}

static int set_eq(const Set *a, const Set *b)
{
    return a->n == b->n && (a->n == 0 || !memcmp(a->v, b->v, (size_t)a->n * sizeof *a->v));
}

static void eps_close(const NFA *a, Set *s)
{
    for (int i = 0; i < s->n; i++)
        for (int k = 0; k < a->ne; k++)
            if (a->e[k].sym == -1 && a->e[k].from == s->v[i]) set_add(s, a->e[k].to);
}

/* DFA for L(exception) with accepting flags inverted, so it accepts the complement */
static void build_complement_dfa(const NFA *a, int start, int final, int nt, DFA *d)
{
    Set *states = NULL;
    int n = 0, cap = 0;
    Set s0 = {0};
    set_add(&s0, start);
    eps_close(a, &s0);
    states = xrealloc(states, sizeof *states); states[n++] = s0; cap = 1;
    d->delta = NULL;
    for (int i = 0; i < n; i++) {
        d->delta = xrealloc(d->delta, (size_t)(i + 1) * (size_t)nt * sizeof *d->delta);
        for (int t = 0; t < nt; t++) {
            Set nx = {0};
            for (int j = 0; j < states[i].n; j++)
                for (int k = 0; k < a->ne; k++)
                    if (a->e[k].from == states[i].v[j] && a->e[k].sym == t) set_add(&nx, a->e[k].to);
            eps_close(a, &nx);
            int found = -1;
            for (int j = 0; j < n; j++) if (set_eq(&states[j], &nx)) { found = j; break; }
            if (found < 0) {
                if (n == cap) { cap *= 2; states = xrealloc(states, (size_t)cap * sizeof *states); }
                states[n] = nx;
                found = n++;
            } else {
                free(nx.v);
            }
            d->delta[i * nt + t] = found;
        }
    }
    d->nstates = n;
    d->start = 0;
    d->accept = xmalloc((size_t)n);
    for (int i = 0; i < n; i++) {
        int acc = 0;
        for (int j = 0; j < states[i].n; j++) if (states[i].v[j] == final) acc = 1;
        d->accept[i] = (unsigned char)!acc;      /* complement */
        free(states[i].v);
    }
    free(states);
}

/* --- product of the factor's sub-grammar with the DFA --- */

typedef struct { int B, p, q; } Key;
typedef struct { Key lhs; int len; Ref rhs[MAXRHS]; int from_prod; } CProd;  /* rhs nonterminals index cand[] */

typedef struct {
    Grammar *g;
    const DFA *d;
    int *sub, nsub;            /* sub-grammar nonterminals in BFS order */
    Key *cand; int ncand;      /* candidate product nonterminals */
    CProd *cp; int ncp, cpcap;
} Product;

static int cand_index(Product *P, int B, int p, int q)
{
    for (int i = 0; i < P->ncand; i++)
        if (P->cand[i].B == B && P->cand[i].p == p && P->cand[i].q == q) return i;
    return -1;
}

static int sub_index(const Product *P, int B)
{
    for (int i = 0; i < P->nsub; i++) if (P->sub[i] == B) return i;
    return -1;
}

static void sub_grammar(Loader *L, Product *P, int A)
{
    Grammar *g = L->g;
    P->sub = xmalloc(sizeof *P->sub);
    P->sub[0] = A; P->nsub = 1;
    for (int i = 0; i < P->nsub; i++) {
        int B = P->sub[i];
        resolve_nt(L, B);
        for (int p = 0; p < g->np; p++) {
            if (g->prod[p].lhs != B) continue;
            for (int k = 0; k < g->prod[p].m; k++) {
                int X = g->prod[p].rhs[g->prod[p].ntpos[k]].id;
                if (sub_index(P, X) < 0) {
                    P->sub = xrealloc(P->sub, (size_t)(P->nsub + 1) * sizeof *P->sub);
                    P->sub[P->nsub++] = X;
                }
            }
        }
    }
}

/* enumerate state paths through production pr starting in state p */
static void paths(Product *P, const Prod *pr, int from_prod, int p, int k, int s, CProd *acc)
{
    const Grammar *g = P->g;
    if (k == pr->len) {
        acc->lhs = (Key){ pr->lhs, p, s };
        acc->len = pr->len;
        acc->from_prod = from_prod;
        if (P->ncp == P->cpcap) { P->cpcap = P->cpcap ? 2 * P->cpcap : 64; P->cp = xrealloc(P->cp, (size_t)P->cpcap * sizeof *P->cp); }
        P->cp[P->ncp++] = *acc;
        return;
    }
    Ref x = pr->rhs[k];
    if (!x.is_nt) {
        acc->rhs[k] = x;
        paths(P, pr, from_prod, p, k + 1, P->d->delta[s * g->nt + x.id], acc);
        return;
    }
    for (int q = 0; q < P->d->nstates; q++) {
        acc->rhs[k] = (Ref){ 1, cand_index(P, x.id, s, q) };
        paths(P, pr, from_prod, p, k + 1, q, acc);
    }
}

static void resolve_exception(Loader *L, const Pending *pe)
{
    Grammar *g = L->g;

    /* automaton for the exception */
    NFA a = {0};
    int s = nfa_state(&a), t = nfa_state(&a);
    NfaCtx c = { L, {0}, pe->line };
    nfa_sym(&c, &a, pe->except, s, t);
    free(c.visiting.v);
    DFA d;
    build_complement_dfa(&a, s, t, g->nt, &d);
    free(a.e);

    if (!pe->factor.is_nt) {
        /* a single terminal: keep it if the complement accepts it */
        int q = d.delta[d.start * g->nt + pe->factor.id];
        if (d.accept[q]) add_unary(g, pe->result, pe->factor, -1);
        free(d.delta); free(d.accept);
        return;
    }

    Product P = { g, &d, NULL, 0, NULL, 0, NULL, 0, 0 };
    sub_grammar(L, &P, pe->factor.id);
    int Q = d.nstates;
    P.ncand = P.nsub * Q * Q;
    P.cand = xmalloc((size_t)P.ncand * sizeof *P.cand);
    for (int i = 0; i < P.nsub; i++)
        for (int p = 0; p < Q; p++)
            for (int q = 0; q < Q; q++)
                P.cand[(i * Q + p) * Q + q] = (Key){ P.sub[i], p, q };

    /* candidate productions, grouped by candidate nonterminal in canonical order */
    for (int i = 0; i < P.nsub; i++)
        for (int p = 0; p < Q; p++)
            for (int pr = 0; pr < g->np; pr++) {
                if (g->prod[pr].lhs != P.sub[i]) continue;
                CProd acc;
                memset(&acc, 0, sizeof acc);
                paths(&P, &g->prod[pr], pr, p, 0, p, &acc);
            }

    /* productive candidates: some production with all symbols productive */
    unsigned char *prodv = xcalloc((size_t)P.ncand, 1);
    for (int changed = 1; changed;) {
        changed = 0;
        for (int i = 0; i < P.ncp; i++) {
            const CProd *cp = &P.cp[i];
            int ci = cand_index(&P, cp->lhs.B, cp->lhs.p, cp->lhs.q);
            if (prodv[ci]) continue;
            int ok = 1;
            for (int k = 0; k < cp->len && ok; k++) if (cp->rhs[k].is_nt && !prodv[cp->rhs[k].id]) ok = 0;
            if (ok) { prodv[ci] = 1; changed = 1; }
        }
    }
    unsigned char *live_prod = xcalloc((size_t)P.ncp, 1);
    for (int i = 0; i < P.ncp; i++) {
        const CProd *cp = &P.cp[i];
        int ok = prodv[cand_index(&P, cp->lhs.B, cp->lhs.p, cp->lhs.q)];
        for (int k = 0; k < cp->len && ok; k++) if (cp->rhs[k].is_nt && !prodv[cp->rhs[k].id]) ok = 0;
        live_prod[i] = (unsigned char)ok;
    }

    /* the result inlines the productions of A[start,q] for accepting q;
     * reachability starts from the symbols of those productions */
    unsigned char *reach = xcalloc((size_t)P.ncand, 1);
    for (int i = 0; i < P.ncp; i++) {
        const CProd *cp = &P.cp[i];
        if (!live_prod[i] || cp->lhs.B != pe->factor.id || cp->lhs.p != d.start || !d.accept[cp->lhs.q]) continue;
        for (int k = 0; k < cp->len; k++) if (cp->rhs[k].is_nt) reach[cp->rhs[k].id] = 1;
    }
    for (int changed = 1; changed;) {
        changed = 0;
        for (int i = 0; i < P.ncp; i++) {
            const CProd *cp = &P.cp[i];
            if (!live_prod[i] || !reach[cand_index(&P, cp->lhs.B, cp->lhs.p, cp->lhs.q)]) continue;
            for (int k = 0; k < cp->len; k++)
                if (cp->rhs[k].is_nt && !reach[cp->rhs[k].id]) { reach[cp->rhs[k].id] = 1; changed = 1; }
        }
    }

    /* emit live product nonterminals, then productions */
    int *newid = xmalloc((size_t)P.ncand * sizeof *newid);
    for (int i = 0; i < P.ncand; i++) {
        newid[i] = -1;
        if (!reach[i] || !prodv[i]) continue;
        char buf[600];
        snprintf(buf, sizeof buf, "%s[%d,%d]", g->ntname[P.cand[i].B], P.cand[i].p, P.cand[i].q);
        char *key = make_key(buf, strlen(buf));
        newid[i] = add_nt(g, buf, key, 1);
        free(key);
    }
    for (int q = 0; q < Q; q++) {          /* result: inlined A[start,q] */
        if (!d.accept[q]) continue;
        for (int i = 0; i < P.ncp; i++) {
            const CProd *cp = &P.cp[i];
            if (!live_prod[i] || cp->lhs.B != pe->factor.id || cp->lhs.p != d.start || cp->lhs.q != q) continue;
            Prod pr;
            memset(&pr, 0, sizeof pr);
            pr.lhs = pe->result;
            for (int k = 0; k < cp->len; k++)
                pr.rhs[pr.len++] = cp->rhs[k].is_nt ? (Ref){ 1, newid[cp->rhs[k].id] } : cp->rhs[k];
            add_prod(g, &pr);
        }
    }
    for (int ci = 0; ci < P.ncand; ci++) {
        if (newid[ci] < 0) continue;
        for (int i = 0; i < P.ncp; i++) {
            const CProd *cp = &P.cp[i];
            if (!live_prod[i] || cand_index(&P, cp->lhs.B, cp->lhs.p, cp->lhs.q) != ci) continue;
            Prod pr;
            memset(&pr, 0, sizeof pr);
            pr.lhs = newid[ci];
            for (int k = 0; k < cp->len; k++)
                pr.rhs[pr.len++] = cp->rhs[k].is_nt ? (Ref){ 1, newid[cp->rhs[k].id] } : cp->rhs[k];
            add_prod(g, &pr);
        }
    }

    free(newid); free(reach); free(live_prod); free(prodv);
    free(P.cp); free(P.cand); free(P.sub);
    free(d.delta); free(d.accept);
}

/* make sure nonterminal A has its final productions (it may be the result of
 * an exception not yet resolved) */
static void resolve_nt(Loader *L, int A)
{
    if (*flag(&L->resolving, A)) die("exception in the definition of '%s' depends on itself", L->g->ntname[A]);
    for (int i = 0; i < L->npend; i++) {
        if (L->pend[i].result != A) continue;
        *flag(&L->resolving, A) = 1;
        Pending pe = L->pend[i];
        /* drop from the pending list before resolving, so it is done once */
        memmove(L->pend + i, L->pend + i + 1, (size_t)(L->npend - i - 1) * sizeof *L->pend);
        L->npend--;
        resolve_exception(L, &pe);
        *flag(&L->resolving, A) = 0;
        return;
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

    /* pass 1: nonterminals are the names on a left-hand side */
    for (int i = 0; i < L.ntok; i++) {
        int arrow = lhs_arrow(&L, i);
        if (arrow < 0) continue;
        const char *s; size_t len;
        span(&L, i, arrow, &s, &len);
        char *key = make_key(s, len), *disp = make_display(s, len);
        if (find_nt_key(g, key) < 0) add_nt(g, disp, key, 0);
        free(key); free(disp);
        i = arrow;
    }
    if (g->nn == 0) die("%s: no rules found", path);

    /* pass 2: productions, desugaring EBNF on the way */
    L.i = 0;
    while (L.tok[L.i].kind != TK_EOF) {
        int arrow = lhs_arrow(&L, L.i);
        if (arrow < 0) tok_error(&L.tok[L.i], "expected 'Name ->'");
        const char *s; size_t len;
        span(&L, L.i, arrow, &s, &len);
        char *key = make_key(s, len);
        int lhs = find_nt_key(g, key);
        free(key);
        L.cur_rule = g->ntname[lhs];
        L.i = arrow + 1;
        parse_alts(&L, lhs, -1);
        if (L.tok[L.i].kind == TK_END) L.i++;   /* optional ISO-style terminator */
    }

    /* pass 3: exceptions, now that every terminal and rule is known */
    while (L.npend) resolve_nt(&L, L.pend[0].result);

    g->plist = xmalloc((size_t)g->nn * sizeof *g->plist);
    g->pcount = xmalloc((size_t)g->nn * sizeof *g->pcount);
    for (int a = 0; a < g->nn; a++) {
        g->pcount[a] = 0;
        for (int p = 0; p < g->np; p++) if (g->prod[p].lhs == a) g->pcount[a]++;
        g->plist[a] = xmalloc((size_t)g->pcount[a] * sizeof **g->plist);
        int k = 0;
        for (int p = 0; p < g->np; p++) if (g->prod[p].lhs == a) g->plist[a][k++] = p;
    }
    free(L.resolving.v);
    free(L.pend);
    free(L.tok);
    free(text);
}

int grammar_is_generated(const Grammar *g, int nt)
{
    return g->generated[nt];
}

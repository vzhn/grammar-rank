/*
 * cfg shape-rank    G                      index of the shape of grammar G
 * cfg shape-unrank  N [--names 'S A B']    the N-th shape, as a grammar file
 * cfg shape-list    K [--names ...]        the first K shapes
 * cfg shape-count   S                      shapes per size up to S
 * cfg grammar-rank  G [--alphabet 'a b']   index of G among grammars over the alphabet
 *                                          (default: G's own terminals, sorted)
 * cfg grammar-unrank N --alphabet 'a b' [--names ...]
 * cfg grammar-list   K --alphabet 'a b' [--names ...]
 * cfg grammar-count  S --alphabet 'a b'
 * cfg shape-selftest S [--alphabet 'a b']  brute force against the tables
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gmp.h>
#include "grammar.h"
#include "shape.h"
#include "shapecmd.h"
#include "shaperank.h"
#include "shapetable.h"
#include "util.h"

typedef struct { char **v; int n; } Words;

static void words_split(Words *w, const char *spec)
{
    w->v = NULL;
    w->n = 0;
    char *copy = xstrndup(spec, strlen(spec));
    for (char *p = strtok(copy, " \t\n"); p; p = strtok(NULL, " \t\n")) {
        for (int i = 0; i < w->n; i++)
            if (!strcmp(w->v[i], p)) die("'%s' is listed twice", p);
        w->v = xrealloc(w->v, (size_t)(w->n + 1) * sizeof *w->v);
        w->v[w->n++] = xstrndup(p, strlen(p));
    }
    free(copy);
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* the terminals of the shape (unreachable rules excluded), sorted */
static void words_from_terminals(Words *w, const Grammar *g, const int *tfirst, int k)
{
    w->n = k;
    w->v = xmalloc((size_t)(k ? k : 1) * sizeof *w->v);
    for (int j = 0; j < k; j++) w->v[j] = g->tname[tfirst[j]];
    qsort(w->v, (size_t)w->n, sizeof *w->v, cmp_str);
}

static int words_index(const Words *w, const char *s)
{
    for (int i = 0; i < w->n; i++)
        if (!strcmp(w->v[i], s)) return i;
    return -1;
}

/* command line: positional arguments plus --alphabet and --names */
typedef struct {
    const char *cmd;
    const char *arg[4];
    int nargs;
    const char *alphabet, *names;
} Args;

static void parse_args(Args *a, int argc, char **argv)
{
    memset(a, 0, sizeof *a);
    a->cmd = argv[1];
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--alphabet") && i + 1 < argc) a->alphabet = argv[++i];
        else if (!strcmp(argv[i], "--names") && i + 1 < argc) a->names = argv[++i];
        else if (a->nargs < 4) a->arg[a->nargs++] = argv[i];
        else die("too many arguments");
    }
}

static int usage(void)
{
    fputs("usage: cfg shape-rank G | shape-unrank N [--names 'S A'] | shape-list K | shape-count S\n"
          "       cfg grammar-rank G [--alphabet 'a b'] | grammar-unrank N --alphabet 'a b' [--names 'S A']\n"
          "       cfg grammar-list K --alphabet 'a b' | grammar-count S --alphabet 'a b'\n"
          "       cfg shape-selftest S [--alphabet 'a b']\n", stderr);
    return 1;
}

static void parse_number(mpz_t num, const char *s)
{
    if (mpz_set_str(num, s, 10) != 0 || mpz_sgn(num) < 0) die("not a natural number: %s", s);
}

static int parse_small(const char *s, const char *what)
{
    char *end;
    long v = strtol(s, &end, 10);
    if (*s == 0 || *end || v < 0 || v > 1000) die("%s must be a number between 0 and 1000: %s", what, s);
    return (int)v;
}

/* load G, take its shape, report dropped nonterminals */
static void load_shape(const char *path, Grammar *g, Shape *s, int **tfirst)
{
    grammar_load(g, path);
    int dropped = shape_of_grammar(g, s, tfirst, NULL);
    if (dropped)
        fprintf(stderr, "cfg: %d unreachable nonterminal%s not part of the shape\n", dropped, dropped == 1 ? " is" : "s are");
}

/* the spelling p[j] = alphabet index of shape terminal j */
static int *spelling(const Grammar *g, const Words *alpha, const int *tfirst, int k)
{
    int *p = xmalloc((size_t)(k ? k : 1) * sizeof *p);
    for (int j = 0; j < k; j++) {
        p[j] = words_index(alpha, g->tname[tfirst[j]]);
        if (p[j] < 0) die("terminal '%s' is not in the alphabet", g->tname[tfirst[j]]);
    }
    return p;
}

/* print the shape with its terminals spelled by p over alpha */
static void print_grammar(const Shape *s, const Words *alpha, const int *p, const Words *names, int oneline, FILE *f)
{
    const char **tn = NULL;
    if (alpha) {
        tn = xmalloc((size_t)(s->nt ? s->nt : 1) * sizeof *tn);
        for (int j = 0; j < s->nt; j++) tn[j] = alpha->v[p[j]];
    }
    shape_print(s, names ? (const char *const *)names->v : NULL, names ? names->n : 0, tn, oneline, f);
    free(tn);
}

/* ---- self-test ---------------------------------------------------------- */

typedef struct { Shape *v; int n, cap; } ShapeList;

/* every shape of exactly n actions, in the order of the ranking */
static void gen(ShapeList *out, Shape *cur, int n, int m, int r, int k)
{
    int left = n - cur->n;
    if (left == 0) {
        if (r != 0) return;
        if (out->n == out->cap) {
            out->cap = out->cap ? 2 * out->cap : 64;
            out->v = xrealloc(out->v, (size_t)out->cap * sizeof *out->v);
        }
        shape_copy(&out->v[out->n++], cur);
        return;
    }
    if (r == 0 || r > left) return;
    int save_n = cur->n, save_nn = cur->nn, save_nt = cur->nt;
#define TRY(kind, id, M, R, K) do { shape_push(cur, kind, id); gen(out, cur, n, M, R, K); \
        cur->n = save_n; cur->nn = save_nn; cur->nt = save_nt; } while (0)
    for (int j = 0; j < k; j++) TRY(A_T, j, m, r, k);
    TRY(A_T, k, m, r, k + 1);
    for (int i = 0; i <= m; i++) TRY(A_N, i, m, r, k);
    TRY(A_N, m + 1, m + 1, r + 1, k);
    TRY(A_BAR, 0, m, r, k);
    TRY(A_END, 0, m, r - 1, k);
#undef TRY
}

static void st_fail(int *fails, const char *what, const Shape *s)
{
    (*fails)++;
    fprintf(stderr, "FAIL: %s", what);
    if (s) { fputs(": ", stderr); shape_print(s, NULL, 0, NULL, 1, stderr); }
    fputc('\n', stderr);
}

/* all k-permutations of 0..sigma-1 in lexicographic order, checked against the grammar index */
static void check_spellings(int *fails, ShapeTable *gt, const Shape *s, int sigma, int k, int i,
                            int *p, unsigned char *used, mpz_t expect, mpz_t got, mpz_t rest, mpz_t shape_part)
{
    if (i == k) {
        perm_rank(got, sigma, p, k);
        mpz_add(got, got, shape_part);
        if (mpz_cmp(got, expect) != 0) st_fail(fails, "grammar rank disagrees with the enumeration", s);
        Shape u;
        shape_unrank(gt, rest, expect, &u);
        if (!shape_eq(&u, s)) st_fail(fails, "grammar unrank returns another shape", s);
        int *q = xmalloc((size_t)(k ? k : 1) * sizeof *q);
        perm_unrank(q, sigma, k, rest);
        if (memcmp(q, p, (size_t)k * sizeof *q)) st_fail(fails, "grammar unrank returns another spelling", s);
        free(q);
        shape_free(&u);
        mpz_add_ui(expect, expect, 1);
        return;
    }
    for (int x = 0; x < sigma; x++) {
        if (used[x]) continue;
        used[x] = 1;
        p[i] = x;
        check_spellings(fails, gt, s, sigma, k, i + 1, p, used, expect, got, rest, shape_part);
        used[x] = 0;
    }
}

static int selftest(int S, int sigma)
{
    int fails = 0;
    ShapeTable st, gt;
    stab_init(&st, -1);
    stab_init(&gt, sigma);
    stab_ensure(&st, S);
    stab_ensure(&gt, S);
    mpz_t expect, got, rest, gexpect, shape_part;
    mpz_init(expect);
    mpz_init(got);
    mpz_init(rest);
    mpz_init(gexpect);
    mpz_init(shape_part);
    int *p = xmalloc((size_t)(S + 1) * sizeof *p);
    unsigned char *used = xcalloc((size_t)(sigma + 1), 1);
    long total_shapes = 0, total_grammars = 0;
    for (int n = 0; n <= S; n++) {
        ShapeList all = { NULL, 0, 0 };
        Shape cur;
        shape_init(&cur);
        gen(&all, &cur, n, 0, 1, 0);
        shape_free(&cur);
        if (mpz_cmp_ui(stab_at(&st, n, 0, 1, 0), (unsigned long)all.n) != 0) {
            char buf[128];
            snprintf(buf, sizeof buf, "size %d: table says %ld shapes, brute force %d", n,
                     mpz_get_ui(stab_at(&st, n, 0, 1, 0)), all.n);
            st_fail(&fails, buf, NULL);
        }
        mpz_set(expect, st.off[n]);
        mpz_set(gexpect, gt.off[n]);
        for (int i = 0; i < all.n; i++) {
            const Shape *s = &all.v[i];
            shape_rank(&st, got, s);
            if (mpz_cmp(got, expect) != 0) st_fail(&fails, "shape rank disagrees with the enumeration", s);
            Shape u;
            shape_unrank(&st, rest, expect, &u);
            if (!shape_eq(&u, s) || mpz_sgn(rest) != 0) st_fail(&fails, "shape unrank disagrees with the enumeration", s);
            shape_free(&u);
            mpz_add_ui(expect, expect, 1);
            if (s->nt <= sigma) {
                shape_rank(&gt, shape_part, s);
                check_spellings(&fails, &gt, s, sigma, s->nt, 0, p, used, gexpect, got, rest, shape_part);
            }
        }
        if (mpz_cmp(expect, st.off[n + 1]) != 0) st_fail(&fails, "shape offsets disagree with the enumeration", NULL);
        if (mpz_cmp(gexpect, gt.off[n + 1]) != 0) st_fail(&fails, "grammar offsets disagree with the enumeration", NULL);
        total_shapes += all.n;
        total_grammars = (long)mpz_get_ui(gexpect);
        for (int i = 0; i < all.n; i++) shape_free(&all.v[i]);
        free(all.v);
    }
    /* round trips on larger indices */
    const char *big[] = { "12345", "9876543210", "123456789012345678901234567890",
                          "31415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679" };
    for (size_t i = 0; i < sizeof big / sizeof *big; i++) {
        parse_number(expect, big[i]);
        Shape u;
        shape_unrank(&st, rest, expect, &u);
        shape_rank(&st, got, &u);
        if (mpz_cmp(got, expect) != 0) st_fail(&fails, "shape round trip", &u);
        shape_free(&u);
        shape_unrank(&gt, rest, expect, &u);
        shape_rank(&gt, got, &u);
        perm_unrank(p, sigma, u.nt, rest);
        perm_rank(rest, sigma, p, u.nt);
        mpz_add(got, got, rest);
        if (mpz_cmp(got, expect) != 0) st_fail(&fails, "grammar round trip", &u);
        shape_free(&u);
    }
    printf("%ld shapes and %ld grammars over %d terminal%s up to size %d, 4 big round trips: %s\n",
           total_shapes, total_grammars, sigma, sigma == 1 ? "" : "s", S, fails ? "FAILED" : "selftest OK");
    free(p);
    free(used);
    mpz_clear(expect);
    mpz_clear(got);
    mpz_clear(rest);
    mpz_clear(gexpect);
    mpz_clear(shape_part);
    stab_free(&st);
    stab_free(&gt);
    return fails ? 1 : 0;
}

/* ---- commands ----------------------------------------------------------- */

int shape_commands(int argc, char **argv)
{
    static const char *const cmds[] = { "shape-rank", "shape-unrank", "shape-list", "shape-count",
        "grammar-rank", "grammar-unrank", "grammar-list", "grammar-count", "shape-selftest", NULL };
    if (argc < 2) return -1;
    int known = 0;
    for (int i = 0; cmds[i]; i++) known |= !strcmp(argv[1], cmds[i]);
    if (!known) return -1;

    Args a;
    parse_args(&a, argc, argv);
    if (a.nargs != 1) return usage();
    int is_grammar = !strncmp(a.cmd, "grammar-", 8);
    const char *sub = a.cmd + (is_grammar ? 8 : 6);

    Words alpha = { NULL, 0 }, names = { NULL, 0 };
    if (a.alphabet) words_split(&alpha, a.alphabet);
    if (a.names) words_split(&names, a.names);
    if (is_grammar && !a.alphabet && strcmp(sub, "rank")) die("%s needs --alphabet 'a b ...'", a.cmd);

    mpz_t num, rest;
    mpz_init(num);
    mpz_init(rest);

    if (!strcmp(sub, "selftest"))
        return selftest(parse_small(a.arg[0], "size"), a.alphabet ? alpha.n : 2);

    if (!strcmp(sub, "rank")) {
        Grammar g;
        Shape s;
        int *tfirst;
        load_shape(a.arg[0], &g, &s, &tfirst);
        if (is_grammar && !a.alphabet) words_from_terminals(&alpha, &g, tfirst, s.nt);
        ShapeTable t;
        stab_init(&t, is_grammar ? alpha.n : -1);
        shape_rank(&t, num, &s);
        if (is_grammar) {
            int *p = spelling(&g, &alpha, tfirst, s.nt);
            perm_rank(rest, alpha.n, p, s.nt);
            mpz_add(num, num, rest);
        }
        mpz_out_str(stdout, 10, num);
        putchar('\n');
        return 0;
    }

    ShapeTable t;
    stab_init(&t, is_grammar ? alpha.n : -1);
    int *p = NULL;

    if (!strcmp(sub, "unrank")) {
        parse_number(num, a.arg[0]);
        Shape s;
        shape_unrank(&t, rest, num, &s);
        if (is_grammar) { p = xmalloc((size_t)(s.nt + 1) * sizeof *p); perm_unrank(p, alpha.n, s.nt, rest); }
        print_grammar(&s, is_grammar ? &alpha : NULL, p, &names, 0, stdout);
        putchar('\n');
        return 0;
    }
    if (!strcmp(sub, "list")) {
        long K = atol(a.arg[0]);
        for (long i = 0; i < K; i++) {
            mpz_set_si(num, i);
            Shape s;
            shape_unrank(&t, rest, num, &s);
            if (is_grammar) { p = xmalloc((size_t)(s.nt + 1) * sizeof *p); perm_unrank(p, alpha.n, s.nt, rest); }
            printf("%ld\t", i);
            print_grammar(&s, is_grammar ? &alpha : NULL, p, &names, 1, stdout);
            putchar('\n');
            free(p);
            p = NULL;
            shape_free(&s);
        }
        return 0;
    }
    if (!strcmp(sub, "count")) {
        int S = parse_small(a.arg[0], "size");
        stab_ensure(&t, S);
        if (is_grammar) {
            ShapeTable st;
            stab_init(&st, -1);
            stab_ensure(&st, S);
            printf("size\tshapes\tgrammars\tfirst index\n");
            for (int n = 0; n <= S; n++)
                gmp_printf("%d\t%Zd\t%Zd\t%Zd\n", n, stab_at(&st, n, 0, 1, 0), stab_at(&t, n, 0, 1, 0), t.off[n]);
        } else {
            printf("size\tshapes\tfirst index\n");
            for (int n = 0; n <= S; n++)
                gmp_printf("%d\t%Zd\t%Zd\n", n, stab_at(&t, n, 0, 1, 0), t.off[n]);
        }
        return 0;
    }
    return usage();
}

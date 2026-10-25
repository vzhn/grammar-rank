#define _GNU_SOURCE   /* open_memstream */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "selftest.h"
#include "enumerate.h"
#include "parser.h"
#include "rank.h"
#include "tree.h"
#include "util.h"

static int failures;

static void fail(const Grammar *g, const char *what, const Node *n)
{
    failures++;
    fprintf(stderr, "FAIL: %s", what);
    if (n) { fputs(": ", stderr); print_tree(g, n, stderr); }
    fputc('\n', stderr);
}

/* print the string, tokenize it again, parse it again, compare trees */
static void check_reparse(const Grammar *g, const Node *t)
{
    char *buf = NULL;
    size_t bl = 0;
    FILE *mf = open_memstream(&buf, &bl);
    print_string(g, t, mf);
    fclose(mf);
    Tokens tk;
    tokenize(g, buf, &tk);
    Node *q = parse(g, &tk);
    if (!node_eq(q, t)) fail(g, "string does not reparse to the same tree", t);
    node_free(q);
    tokens_free(&tk);
    free(buf);
}

/* set of yields seen so far: two trees with the same yield mean ambiguity */
typedef struct Seen { char *key; struct Seen *next; } Seen;
enum { NB = 8191 };

static unsigned long hash(const char *s)
{
    unsigned long h = 5381;
    while (*s) h = h * 33 + (unsigned char)*s++;
    return h;
}

static int seen_before(Seen **tab, char *key)
{
    unsigned long h = hash(key) % NB;
    for (Seen *e = tab[h]; e; e = e->next)
        if (!strcmp(e->key, key)) { free(key); return 1; }
    Seen *e = xmalloc(sizeof *e);
    e->key = key; e->next = tab[h]; tab[h] = e;
    return 0;
}

/* derivation size of the string with index num, or -1 if it would exceed
 * cap or the language has fewer strings */
static int index_size(Table *tb, const mpz_t num, int cap)
{
    const Grammar *g = tb->g;
    if (g->finite && cap > g->max_size) cap = g->max_size;
    for (int s = 0; s <= cap; s++) {
        table_ensure(tb, s);
        if (mpz_cmp(tb->off[s + 1], num) > 0) return s;
    }
    return -1;
}

int selftest(Table *tb)
{
    const Grammar *g = tb->g;
    mpz_t r, want;
    mpz_inits(r, want, NULL);
    unsigned long total = 0;
    Seen **seen = xcalloc(NB, sizeof *seen);

    for (int s = 0; s <= 60 && total < 5000 && !(g->finite && s > g->max_size); s++) {
        const TreeList *all = enumerate(g, 0, s);
        table_ensure(tb, s);
        if (mpz_cmp_ui(tb->N[0].v[s], (unsigned long)all->n) != 0)
            fail(g, "count disagrees with brute force", NULL);
        for (size_t i = 0; i < all->n; i++) {
            Node *t = all->v[i];
            rank(tb, r, t);
            mpz_add_ui(want, tb->off[s], (unsigned long)i);
            if (mpz_cmp(r, want) != 0) fail(g, "rank differs from brute-force position", t);
            Node *u = unrank(tb, want);
            if (!node_eq(u, t)) fail(g, "unrank differs from brute-force tree", t);
            node_free(u);
            if (seen_before(seen, yield_key(g, t))) fail(g, "two derivations yield the same string", t);
            check_reparse(g, t);
        }
        printf("size %2d: %6zu strings ok\n", s, all->n);
        total += all->n;
    }
    for (int i = 0; i < NB; i++)
        for (Seen *e = seen[i]; e;) { Seen *nx = e->next; free(e->key); free(e); e = nx; }
    free(seen);

    /* round trips of consecutive indices, bounded by count and by derivation
     * size: a language with one string per size would otherwise ask for
     * trees with thousands of nodes */
    unsigned long limit = 20000, i;
    for (i = 0; i < limit; i++) {
        mpz_set_ui(want, i);
        if (index_size(tb, want, 300) < 0) break;
        Node *t = unrank(tb, want);
        rank(tb, r, t);
        if (mpz_cmp(r, want) != 0) fail(g, "rank(unrank(i)) != i", t);
        node_free(t);
    }
    printf("round trip 0..%lu ok\n", i ? i - 1 : 0);

    const char *big[] = { "1000000000000", "123456789012345678901234567890",
                          "99999999999999999999999999999999999999999999999999" };
    for (size_t b = 0; b < sizeof big / sizeof *big; b++) {
        mpz_set_str(want, big[b], 10);
        int s = index_size(tb, want, 2000);
        if (s < 0) { printf("%s -> beyond size 2000 or the language, skipped\n", big[b]); continue; }
        Node *t = unrank(tb, want);
        rank(tb, r, t);
        if (mpz_cmp(r, want) != 0) fail(g, "big round trip", t);
        printf("%s -> size %d string ok\n", big[b], t->size);
        node_free(t);
    }
    mpz_clears(r, want, NULL);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("selftest OK");
    return 0;
}

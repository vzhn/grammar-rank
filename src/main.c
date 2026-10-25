/*
 * cfg -- a bijection between the strings of an LL(1) language and the
 * natural numbers, graded by derivation size.
 *
 *   cfg check    G            FIRST/FOLLOW, LL(1) verdict, finiteness
 *   cfg rank     G 'string'   index of a string
 *   cfg unrank   G N [--tree] string with index N
 *   cfg list     G K [--tree] first K strings
 *   cfg count    G S          strings per size up to S
 *   cfg selftest G            brute force + round trips
 *   cfg shape-* / grammar-*   enumerating grammars themselves, see shapecmd.c
 *
 * Input strings are tokenized by longest match against the terminals;
 * whitespace between tokens is ignored.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gmp.h>
#include "analysis.h"
#include "grammar.h"
#include "parser.h"
#include "rank.h"
#include "selftest.h"
#include "shapecmd.h"
#include "table.h"
#include "tree.h"
#include "util.h"

static int usable(const Grammar *g)
{
    return g->ll1_ok || (g->cyclic < 0 && !g->ambiguous);
}

static void print_set(const Grammar *g, const unsigned char *set, int with_end)
{
    int any = 0;
    for (int t = 0; t < g->nt; t++)
        if (set[t]) { printf("%s'%s'", any ? " " : "", g->tname[t]); any = 1; }
    if (with_end && set[g->nt]) { printf("%s$", any ? " " : ""); any = 1; }
    if (!any) printf("-");
}

static int check(const Grammar *g, Table *tb)
{
    printf("start symbol: %s\n", g->ntname[0]);
    printf("terminals:   ");
    for (int t = 0; t < g->nt; t++) printf(" '%s'", g->tname[t]);
    printf("\n\n");
    for (int a = 0; a < g->nn; a++) {
        printf("%s:%s%s%s%s\n", g->ntname[a], grammar_is_generated(g, a) ? " (generated)" : "",
               g->nullable[a] ? " nullable" : "",
               g->productive[a] ? "" : " UNPRODUCTIVE",
               g->reachable[a] || g->in_exception[a] ? "" : " UNREACHABLE");
        printf("  first:  "); print_set(g, g->first[a], 0); putchar('\n');
        printf("  follow: "); print_set(g, g->follow[a], 1); putchar('\n');
        for (int k = 0; k < g->pcount[a]; k++) {
            int p = g->plist[a][k];
            const Prod *pr = &g->prod[p];
            printf("  %d. %s ->", k + 1, g->ntname[a]);
            if (!pr->len) printf(" eps");
            for (int j = 0; j < pr->len; j++)
                printf(pr->rhs[j].is_nt ? " %s" : " '%s'",
                       pr->rhs[j].is_nt ? g->ntname[pr->rhs[j].id] : g->tname[pr->rhs[j].id]);
            printf("   predict: "); print_set(g, g->pred[p], 1); putchar('\n');
        }
    }
    if (g->ll1_ok) {
        printf("\nLL(1): yes\n");
    } else {
        int a = g->conflict_nt, t = g->conflict_t;
        printf("\nLL(1): no (%s: alternatives %d and %d both predicted by %s)\n",
               g->ntname[a], g->conflict_x + 1, g->conflict_y + 1,
               t == g->nt ? "end of input" : g->tname[t]);
        if (g->generated[a] && (strstr(g->ntname[a], ".x") || strchr(g->ntname[a], '[')))
            printf("  (this rule comes from an exception '-')\n");
        if (g->cyclic >= 0) {
            printf("REJECTED: %s derives itself, so every string has infinitely many derivations\n",
                   g->ntname[g->cyclic]);
        } else if (g->ambiguous) {
            printf("REJECTED: ambiguous, '%s' has two derivations:\n  %s\n  %s\n",
                   g->ambig_string, g->ambig_tree1, g->ambig_tree2);
        } else {
            printf("parser: Earley\n");
            if (g->probe_exhaustive)
                printf("ambiguity probe: none; all %ld derivations of the finite language were checked\n",
                       g->probe_trees);
            else
                printf("ambiguity probe: none found among %ld derivations up to size %d\n",
                       g->probe_trees, g->probe_size);
        }
    }
    if (g->finite) {
        table_ensure(tb, g->max_size);
        gmp_printf("language: finite, %Zd strings, largest derivation size %d\n",
                   tb->off[g->max_size + 1], g->max_size);
    } else {
        printf("language: infinite\n");
    }
    return usable(g) ? 0 : 1;
}

static int usage(void)
{
    fputs("usage: cfg check G | rank G STRING | unrank G N [--tree] | list G K [--tree] | count G S | selftest G\n"
          "       cfg shape-rank G | shape-unrank N | shape-list K | shape-count S | shape-selftest S\n"
          "       cfg grammar-rank G [--alphabet 'a b'] | grammar-unrank N --alphabet 'a b' [--names 'S A']\n"
          "       cfg grammar-list K --alphabet 'a b' | grammar-count S --alphabet 'a b'\n",
          stderr);
    return 1;
}

static void parse_number(mpz_t num, const char *s)
{
    if (mpz_set_str(num, s, 10) != 0 || mpz_sgn(num) < 0) die("not a natural number: %s", s);
}

int main(int argc, char **argv)
{
    int rc = shape_commands(argc, argv);
    if (rc >= 0) return rc;
    if (argc < 3) return usage();
    const char *cmd = argv[1];
    int tree = argc > 3 && !strcmp(argv[argc - 1], "--tree");
    if (tree) argc--;

    Grammar g;
    grammar_load(&g, argv[2]);
    analyze(&g);
    Table tb;
    table_init(&tb, &g);

    if (!strcmp(cmd, "check")) return check(&g, &tb);
    if (g.cyclic >= 0)
        die("'%s' derives itself, so every string has infinitely many derivations", g.ntname[g.cyclic]);
    if (g.ambiguous)
        die("ambiguous grammar: '%s' has two derivations:\n  %s\n  %s", g.ambig_string, g.ambig_tree1, g.ambig_tree2);

    if (!strcmp(cmd, "rank") && argc == 4) {
        Tokens tk;
        tokenize(&g, argv[3], &tk);
        Node *n = parse(&g, &tk);
        mpz_t r;
        mpz_init(r);
        rank(&tb, r, n);
        if (tree) { print_tree(&g, n, stdout); putchar('\n'); }
        mpz_out_str(stdout, 10, r);
        putchar('\n');
        return 0;
    }
    if (!strcmp(cmd, "unrank") && argc == 4) {
        mpz_t num;
        mpz_init(num);
        parse_number(num, argv[3]);
        Node *n = unrank(&tb, num);
        if (tree) print_tree(&g, n, stdout); else print_string(&g, n, stdout);
        putchar('\n');
        return 0;
    }
    if (!strcmp(cmd, "list") && argc == 4) {
        long k = atol(argv[3]);
        mpz_t num;
        mpz_init(num);
        if (g.finite) {
            table_ensure(&tb, g.max_size);
            if (mpz_cmp_si(tb.off[g.max_size + 1], k) < 0) k = mpz_get_si(tb.off[g.max_size + 1]);
        }
        for (long i = 0; i < k; i++) {
            mpz_set_si(num, i);
            Node *n = unrank(&tb, num);
            printf("%ld\t", i);
            if (tree) print_tree(&g, n, stdout);
            else if (string_is_empty(&g, n)) fputs("ε", stdout);
            else print_string(&g, n, stdout);
            putchar('\n');
            node_free(n);
        }
        return 0;
    }
    if (!strcmp(cmd, "count") && argc == 4) {
        int S = atoi(argv[3]);
        table_ensure(&tb, S);
        printf("size\tstrings\tfirst index\n");
        for (int s = 0; s <= S; s++)
            gmp_printf("%d\t%Zd\t%Zd\n", s, tb.N[0].v[s], tb.off[s]);
        return 0;
    }
    if (!strcmp(cmd, "selftest")) return selftest(&tb);
    return usage();
}

/*
 * A context-free grammar read from a file, plus the analysis results that
 * analyze() attaches to it.
 *
 * Grammar file:
 *     # comment
 *     E  -> T E1
 *     E1 -> '+' T E1 | eps
 *     F  -> '(' E ')' | 'x'
 *     L  -> 'x' { ',' 'x' } [ ';' ]  ( 'a' | 'b' )+  'c'*  'd'?
 * Quoted text is a terminal, a bare identifier is a nonterminal, `eps` or an
 * empty alternative is the empty string, `|` separates alternatives, `->`,
 * `::=` or `=` separates sides, and a rule may end with `.` or `;`.
 * EBNF operators: `( )` grouping, `[ ]` or `?` optional, `{ }` or `*`
 * repetition, `+` one or more.  They are desugared into generated
 * nonterminals named Rule.gN / .oN / .rN / .pN.  The first nonterminal is
 * the start symbol.
 *
 * ISO/IEC 14977 syntax is accepted as well: `(* comments *)`, `,` between
 * items, `/` or `!` for `|`, `(/ /)` and `(: :)` brackets, `3 * x` integer
 * repetition, meta-identifiers containing spaces (compared with the spaces
 * removed), and the exception `factor - exception`.  An exception must be a
 * regular language, as the standard requires; it is applied by intersecting
 * the factor's sub-grammar with the complement automaton of the exception
 * (generated nonterminals named B[p,q]).  Special sequences `? ... ?` are
 * rejected.
 */
#ifndef GRAMMAR_H
#define GRAMMAR_H

#define MAXRHS 256

typedef struct { int is_nt, id; } Ref;

typedef struct {
    int lhs;
    int len;
    Ref rhs[MAXRHS];
    int m;               /* number of nonterminals in rhs */
    int ntpos[MAXRHS];   /* rhs positions of those nonterminals */
} Prod;

typedef struct {
    /* syntax, filled by grammar_load */
    char **ntname; int nn;       /* nonterminals; 0 is the start symbol */
    char **ntkey;                /* ntname with whitespace removed, for lookup */
    unsigned char *generated;    /* [nn] made by desugaring, not written by the user */
    unsigned char *in_exception; /* [nn] used as an operand of '-'; may be unreachable afterwards */
    char **tname;  int nt;       /* terminals; the end-of-input marker is nt */
    Prod *prod;    int np;       /* productions in file order */
    int **plist;   int *pcount;  /* productions of each nonterminal, grammar order */

    /* derived, filled by analyze */
    unsigned char *nullable, *productive, *reachable;   /* [nn] */
    unsigned char **first;       /* [nn][nt] */
    unsigned char **follow;      /* [nn][nt+1] */
    unsigned char **pred;        /* [np][nt+1] predict sets */
    int ll1_ok;
    int finite;                  /* the language has finitely many strings */
    int max_size;                /* largest derivation size; finite languages only */
} Grammar;

void grammar_load(Grammar *g, const char *path);
int grammar_is_generated(const Grammar *g, int nt);   /* nonterminal made by EBNF desugaring */

#endif

/*
 * Grammar shapes: a grammar with its names erased.
 *
 * Read the grammar from the start symbol, rules in the order their
 * nonterminals were first mentioned, alternatives in order, symbols left to
 * right.  The i-th new nonterminal becomes i (the start symbol is 0) and
 * the j-th new terminal becomes j.  A shape is stored as the sequence of
 * actions that writes it:
 *
 *   A_T id    write terminal id       (id == terminals so far: a new one)
 *   A_N id    write nonterminal id    (id == nonterminals so far: a new one)
 *   A_BAR     end the alternative, start the next
 *   A_END     end the rule
 *
 * The size of a shape is the number of actions.  See
 * docs/grammar-enumeration.md.
 */
#ifndef SHAPE_H
#define SHAPE_H
#include <stdio.h>
#include "grammar.h"

typedef enum { A_T, A_N, A_BAR, A_END } Kind;
typedef struct { Kind kind; int id; } Action;

typedef struct {
    Action *a;
    int n, cap;
    int nn, nt;       /* nonterminals and terminals introduced so far */
} Shape;

void shape_init(Shape *s);
void shape_push(Shape *s, Kind kind, int id);
void shape_copy(Shape *dst, const Shape *src);
void shape_free(Shape *s);
int shape_eq(const Shape *a, const Shape *b);

/*
 * The shape of a grammar.  *tfirst[j] is the grammar's terminal id of shape
 * terminal j and *nfirst[i] the grammar's nonterminal id of shape
 * nonterminal i (both malloc'd; pass NULL to skip).  Returns the number of
 * unreachable nonterminals, which are not part of the shape.
 */
int shape_of_grammar(const Grammar *g, Shape *s, int **tfirst, int **nfirst);

/*
 * Print the shape as a grammar file.  Nonterminal i is called ntnames[i]
 * for i < nntnames and N<i> otherwise; terminal j is tnames[j], or t<j>
 * when tnames is NULL.  With oneline the rules are separated by " ; "
 * instead of newlines.  No trailing newline.
 */
void shape_print(const Shape *s, const char *const *ntnames, int nntnames,
                 const char *const *tnames, int oneline, FILE *f);

#endif

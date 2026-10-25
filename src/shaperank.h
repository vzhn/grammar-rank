/*
 * Rank and unrank of shapes and of terminal spellings.
 *
 * Shapes are graded by size and ordered inside a size class
 * lexicographically over their action sequences, with the actions in the
 * order: old terminals by index, new terminal, old nonterminals by index,
 * new nonterminal, |, ;.  With a counting table for an alphabet (sigma >= 0)
 * the same walk yields the number of grammars whose shape precedes the
 * given one; adding the rank of the spelling gives the grammar's index.
 */
#ifndef SHAPERANK_H
#define SHAPERANK_H
#include <gmp.h>
#include "shape.h"
#include "shapetable.h"

/* out = objects of smaller size + objects whose shape precedes s */
void shape_rank(ShapeTable *t, mpz_t out, const Shape *s);

/* the shape whose block contains num; rest = num's offset in that block
 * (0 for a shape table, the spelling's rank for a grammar table) */
void shape_unrank(ShapeTable *t, mpz_t rest, const mpz_t num, Shape *s);

/* k-permutations p[0..k-1] of 0..sigma-1 in lexicographic order */
void perm_rank(mpz_t out, int sigma, const int *p, int k);
void perm_unrank(int *p, int sigma, int k, const mpz_t num);

#endif

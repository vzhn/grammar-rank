/* Brute-force enumeration of derivation trees, memoized per (nonterminal,
 * size).  Used by the ambiguity probe and the self-test as an oracle that is
 * independent of the counting tables. */
#ifndef ENUMERATE_H
#define ENUMERATE_H
#include <stddef.h>
#include "grammar.h"
#include "tree.h"

typedef struct { Node **v; size_t n, cap; } TreeList;

/* every derivation tree rooted at A with s applications, in the order the
 * numbering uses; owned by the enumerator, valid until exit */
const TreeList *enumerate(const Grammar *g, int A, int s);

#endif

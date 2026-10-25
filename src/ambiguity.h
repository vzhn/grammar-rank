/*
 * For grammars that are not LL(1), unambiguity cannot be decided at load
 * time.  Two checks stand in: a grammar in which some nonterminal derives
 * itself (A =>+ A through unit and nullable productions) gives every string
 * infinitely many derivations and is refused; otherwise a bounded brute-force
 * probe enumerates derivation trees and refuses the grammar if two of them
 * yield the same string, keeping both as a witness.
 */
#ifndef AMBIGUITY_H
#define AMBIGUITY_H
#include "grammar.h"

void ambiguity_check(Grammar *g);   /* fills g->cyclic / g->ambiguous and the witness fields */

#endif

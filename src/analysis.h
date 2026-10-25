#ifndef ANALYSIS_H
#define ANALYSIS_H
#include "grammar.h"

/* Fill the derived fields of g: nullable, FIRST, FOLLOW, predict sets,
 * productivity, reachability, the LL(1) verdict, whether the language is
 * finite, and for non-LL(1) grammars the ambiguity checks. */
void analyze(Grammar *g);

#endif

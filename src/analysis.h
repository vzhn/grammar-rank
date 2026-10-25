#ifndef ANALYSIS_H
#define ANALYSIS_H
#include "grammar.h"

/* Fill the derived fields of g: nullable, FIRST, FOLLOW, predict sets,
 * productivity, reachability, the LL(1) verdict (conflicts are reported on
 * stderr) and whether the language is finite. */
void analyze(Grammar *g);

#endif

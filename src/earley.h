/* Earley parser for grammars that are not LL(1).  Builds the derivation
 * tree and dies if the string has more than one derivation. */
#ifndef EARLEY_H
#define EARLEY_H
#include "grammar.h"
#include "parser.h"
#include "tree.h"

Node *earley_parse(const Grammar *g, const Tokens *tk);

#endif

/* Tokenizer (longest match against the terminals, whitespace ignored) and
 * predictive LL(1) parser.  Both call die() on bad input. */
#ifndef PARSER_H
#define PARSER_H
#include "grammar.h"
#include "tree.h"

typedef struct { int *v; int n; } Tokens;

void tokenize(const Grammar *g, const char *s, Tokens *out);
void tokens_free(Tokens *t);
Node *parse(const Grammar *g, const Tokens *t);

#endif

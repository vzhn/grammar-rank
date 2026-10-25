/* Derivation trees.  A node records which production was applied; its
 * children are the nonterminal positions of that production, in order. */
#ifndef TREE_H
#define TREE_H
#include <stdio.h>
#include "grammar.h"

typedef struct Node {
    int nt, prod;
    int nkids;
    int size;              /* number of production applications, see node_finish */
    struct Node **kids;
} Node;

Node *node_new(const Grammar *g, int nt, int prod);   /* kids are NULL until filled */
void node_finish(Node *n);                            /* compute size once kids are set */
void node_free(Node *n);
int node_eq(const Node *a, const Node *b);

int string_is_empty(const Grammar *g, const Node *n);      /* the tree yields no terminals */
char *yield_key(const Grammar *g, const Node *n);          /* malloc'd "id,id,...": equal iff same string */
void print_string(const Grammar *g, const Node *n, FILE *f);
void print_tree(const Grammar *g, const Node *n, FILE *f);

#endif

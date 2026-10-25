/* The shape-* and grammar-* commands: enumerating grammars themselves. */
#ifndef SHAPECMD_H
#define SHAPECMD_H

/* Returns the exit status, or -1 if argv[1] is not one of these commands. */
int shape_commands(int argc, char **argv);

#endif

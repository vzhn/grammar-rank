# grammar-rank

Recreational C project: a bijection between the strings of an LL(1) language
and the natural numbers, graded by derivation size. See README.md for the
idea and the grammar file syntax.

## Workflow

- Every task starts as a GitHub issue. Work on a branch named after it
  (`issue-N-short-title`), never directly on `master`.
- When the work is done, open a pull request whose description ends with
  `Closes #N`. Do not merge without being asked.
- Plan before implementing anything non-trivial, and wait for approval of
  the plan. Ask before each commit.
- Commit messages: a short imperative subject, a body that says what changed
  and why. Keep the attribution trailer the tooling adds.

## Build and test

```
make          # builds ./cfg, needs GMP (libgmp-dev / gmp-devel / dev-libs/gmp)
make test     # tests/arithmetic.sh golden tests, then `cfg selftest` on every grammar
make clean
```

`make test` must pass before a pull request is opened. The build must stay
free of warnings under `-Wall -Wextra -std=gnu11`.

## Layout

```
src/util.*       die() and checked allocation
src/grammar.*    Grammar struct, scanner, EBNF desugaring, grammar_load()
src/analysis.*   nullable, FIRST, FOLLOW, predict sets, LL(1) check, finiteness
src/table.*      Table struct: GMP counting tables, table_ensure()
src/tree.*       derivation trees, printing
src/parser.*     tokenizer and predictive parser
src/rank.*       rank() and unrank()
src/selftest.*   brute-force oracle and round trips
src/main.c       command line and the `check` report
grammars/        example grammars; ambig.txt is deliberately not LL(1)
tests/           shell tests with golden values
```

## Invariants

- The numbering is part of the contract. A change that alters the index of
  any string in an existing grammar is a breaking change: it must be
  intentional, called out in the PR, and the golden values in `tests/` must be
  updated in the same PR.
- `cfg selftest` is the oracle. New features in the loader or the counting
  code are not done until it passes on a grammar that exercises them, and
  such a grammar belongs in `grammars/` and in the `GRAMMARS` list of the
  Makefile.
- State lives in the `Grammar` and `Table` structs and is passed explicitly.
  No new file-level globals outside `selftest.c`.
- Errors go through `die()` with a line number for grammar files and a token
  position for input strings.

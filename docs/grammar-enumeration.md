# Enumerating grammars

Implemented in `src/shape.c` (canonical form, printing), `src/shapetable.c`
(the counting table), `src/shaperank.c` (rank, unrank, permutations) and
`src/shapecmd.c` (commands and self-test); golden values in
`tests/shapes.sh`. The text below is the plan the implementation follows.

Plan for numbering grammars themselves, the way `cfg rank` numbers the
strings of one grammar. The index of a grammar must not depend on what its
nonterminals are called or on the order of its rules in the file. It may
depend on the order of alternatives and on the order of symbols inside an
alternative, because the string numbering already does.

## 1. The problem is the names

A grammar is a rooted structure: a start symbol, and for each nonterminal an
ordered list of alternatives, each an ordered sequence of symbols. Two
grammars that differ only in nonterminal names are the same object, so the
things to number are isomorphism classes, as unlabeled graphs are classes of
labeled ones.

For grammars whose alternatives are *unordered sets* this is hard. The rule
`v -> w 'a'` for every edge encodes an arbitrary directed graph, so
canonizing such grammars is as hard as graph canonization, and counting the
classes per size has no recurrence, which ranking needs.

Our grammars are ordered: alternatives form a list and right-hand sides are
sequences. An ordered rooted graph has a canonical labeling in linear time,
and that labeling can be made to disappear. This is the same move that de
Bruijn indices make for lambda terms: instead of comparing named objects up
to renaming, pick a representation in which there is nothing to rename.

## 2. Layering

A named grammar decomposes into three independent pieces.

| layer | what it fixes | how it is ranked |
|---|---|---|
| shape | the structure, with every name erased | four-parameter counting table, action walk |
| terminal spelling | which real terminal each shape terminal is | k-permutation of the declared alphabet |
| nonterminal names | which identifier each nonterminal gets | optional, output only |

The index of a grammar over an alphabet is built from the first two.
Nonterminal names are deliberately left out of the index: they carry no
information about the grammar, and the set of identifiers is infinite, so
there is no canonical alphabet to permute. They come back only when a
grammar is printed.

## 3. Layer 1: shapes

### Canonical form

Read the grammar from the start symbol: rules in the order their
nonterminals were first mentioned, alternatives in order, symbols left to
right. The `i`-th new nonterminal becomes `i` (the start symbol is `0`) and
the `j`-th new terminal becomes `j`. The result is a *shape*:

```
0 -> 1 t0 | t1 ;
1 -> 0 t2 | ;
```

Every grammar in which all nonterminals are reachable has exactly one shape,
and every shape is a grammar. Unreachable nonterminals cannot be expressed,
which is the right notion of a grammar anyway. Rule order in the file is
irrelevant, since the canonical form fixes it.

The defining property is *restricted growth*: when a nonterminal or terminal
appears for the first time, its number is exactly one more than the largest
number of its kind seen so far. The same constraint makes set partitions
countable as restricted growth strings.

### Shapes as action sequences

A shape is most easily counted as a sequence of actions:

| action | meaning |
|---|---|
| `T j` | write terminal `j`, already introduced (`j < k`) |
| `T new` | introduce terminal `k`, which becomes `k + 1` |
| `N i` | write nonterminal `i`, already introduced (`i ≤ m`) |
| `N new` | introduce nonterminal `m + 1`; one more rule is owed |
| `\|` | end the current alternative, start the next |
| `;` | end the current rule; one rule less is owed |

The sequence starts inside the rule of nonterminal `0` with one rule owed and
ends when a `;` brings the number of owed rules to zero, so `C(n, m, 0, k)`
is `0` for `n > 0`. A rule has at least
one alternative; an alternative may be empty. Because nonterminal `i`'s rule
is written after every rule `< i`, and `i` was introduced inside one of
those, every nonterminal is reachable by construction.

### Size

Size is the number of actions, so one per symbol, one per alternative
boundary, one per rule end. Any positive weighting works; this one makes the
size classes finite with no further argument, since a shape of size `n` has
at most `n` symbols and therefore at most `n` nonterminals and terminals.

### Counting

Let `C(n, m, r, k)` be the number of ways to finish a shape with `n` actions
left, `m + 1` nonterminals introduced, `r` rules still owed (`1 ≤ r ≤ m + 1`)
and `k` terminals introduced. Then `C(0, m, 0, k) = 1`, `C(0, m, r, k) = 0`
for `r > 0`, and for `n > 0`

```
C(n, m, r, k) =  k · C(n-1, m,   r,   k)        T j, old terminal
              +     C(n-1, m,   r,   k+1)      T new
              + (m+1) · C(n-1, m, r,   k)      N i, old nonterminal
              +     C(n-1, m+1, r+1, k)        N new
              +     C(n-1, m,   r,   k)        |
              +     C(n-1, m,   r-1, k)        ;        (only if r ≥ 1)
```

The table has `O(n⁴)` entries, each a sum of six terms, so no convolution
and no product; it is much cheaper than the string tables. The number of
shapes of size `n` is `C(n, 0, 1, 0)`, and the number of grammars over an alphabet of size `σ` comes from the
same recurrence with the base case `C(0, m, 0, k) = P(σ, k)` instead of
`1`: each completed shape is then counted once per spelling, and shapes
with more than `σ` terminals contribute nothing. The weighted table also
gives, during the rank walk, the number of grammars whose shape precedes
the given one, which is what the composition in section 6 needs.

### Rank and unrank

Inside a size class the order is lexicographic over the action sequence with
the action order of the table above: old terminals by index, new terminal,
old nonterminals by index, new nonterminal, `|`, `;`. Ranking walks the
sequence left to right, adding for each position the counts of all actions
that come earlier in that order; unranking subtracts until the number fits.
This is the ranking of words accepted by a counter machine, and it is simpler
than the tree ranking used for strings because the structure is a sequence.

## 4. Layer 2: terminal spelling

A shape with `k` terminals becomes a grammar over an alphabet `Σ` by an
injection `0..k-1 → Σ`: which real terminal appeared first, which second,
and so on. That is a `k`-permutation of `Σ`, of which there are
`P(|Σ|, k) = |Σ|! / (|Σ| - k)!`, ranked by the factorial number system in
`O(k²)`.

The decomposition is a bijection because a shape is ordered and therefore
has no automorphisms: every grammar over `Σ` is its shape plus the list of
its terminals in first-occurrence order, and nothing else maps to it. With
unordered alternatives this would fail, since a symmetric shape would be
reached by several injections and counting would need Burnside's lemma
instead of a product.

The alphabet has to be declared, because the index of a grammar over
`{a, b}` differs from its index over `{a, b, c}`. For a grammar file the
default is its own terminal set in lexicographic order, so that the index
depends only on the relative order of the terminal names.

## 5. Layer 3: nonterminal names

The same injection ranking applies to nonterminals against a list of
candidate names and costs nothing extra. It is an output option only:
`unrank` prints `N0, N1, …` unless a name list is given, and `rank` ignores
names entirely. This is what makes the index invariant under renaming.

## 6. Composition

Grammars over `Σ` are graded by shape size. Inside a size class the order is
shape-major: all spellings of one shape are consecutive, so neighbouring
indices are relabelings of the same structure and the shape index is
meaningful by itself. The number of grammars of size `n` is

```
G(n) = Σ_k  S(n, k) · P(|Σ|, k)
```

and the index of a grammar is the offset of its size class, plus the sum of
`S(n, k') · P(|Σ|, k')` over shapes that precede it, plus the permutation
rank. Both parts come from the tables already described.

## 7. Commands

```
cfg grammar-rank   G.txt [--alphabet 'a b c']      index of the grammar in G.txt
cfg grammar-unrank N --alphabet 'a b' [--names 'S A B']   the N-th grammar, as a grammar file
cfg grammar-list   K --alphabet 'a'                 the first K grammars
cfg grammar-count  S --alphabet 'a b'               grammars per size up to S
cfg shape-rank / shape-unrank / shape-list          layer 1 alone
```

`grammar-rank` reads the file with the existing loader, so EBNF is accepted
and desugared first; the shape is that of the desugared grammar.

## 8. Verification

- Brute force: enumerate shapes of each size directly from the action
  definition, check the count against the table, and check that the `i`-th
  shape ranks to `i`.
- Round trip: print each shape as a grammar file with generated names,
  reload it, canonicalize, and confirm the same shape and index.
- Invariance: rename nonterminals and permute rule order in the example
  grammars and confirm `grammar-rank` does not change.
- Spelling: for a small alphabet, `grammar-list` must produce each named
  grammar exactly once up to the size bound.

## 9. What it enables

Shapes turn the existing analysis into a survey tool: enumerate every shape
up to some size and count how many are LL(1), how many the ambiguity probe
refuses, how many describe finite languages, how many need Earley. Those are
one loop over `shape-unrank` and `cfg check`.

## 10. Not covered

- Equivalence up to reordering alternatives. Canonical forms exist, after
  merging structurally identical nonterminals, but they identify grammars
  with different derivation trees and the counting recurrence does not
  survive.
- Language equivalence. Undecidable for context-free grammars.
- Grammars with unreachable nonterminals. The canonical form drops them; a
  `grammar-rank` of such a file ranks the reachable part and says so.

## 11. Size of the work

A canonicalizer returning the shape and the two first-occurrence lists, the
four-parameter table, action-sequence rank and unrank, the permutation
ranker, the commands, a self-test and tests with golden values. Roughly the
size of the ISO EBNF change.

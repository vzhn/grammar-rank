# grammar-rank

Every string of a context-free language gets a number, and every number
gets a string back.

```
$ ./cfg list grammars/ebnf.txt 6
0       x
1       x * x
2       x + x
3       x * x * x
4       ( x )
5       x + x * x

$ ./cfg rank grammars/ebnf.txt 'x+x*(x)'
34

$ ./cfg unrank grammars/ebnf.txt 34
x + x * ( x )

$ ./cfg unrank grammars/ebnf.txt 123456789012345678901234567890
( x * x * ( x ) + ( x ) + x * x * ( x * x * x + x * x * x * x * x * x * ( x * x ...
```

This is a recreational programming project. It started as the question
"how would you order all lambda terms?", became a program that numbers
closed de Bruijn terms, and then generalised to any LL(1) grammar. There
is nothing here that is not in the literature from the 1980s, but it was
fun to build and the pieces fit together nicely.

## Building

You need a C compiler and [GMP](https://gmplib.org/), the GNU multiple
precision library. On Debian or Ubuntu that is `libgmp-dev`, on Fedora
`gmp-devel`, on Gentoo `dev-libs/gmp`, on macOS `brew install gmp`.

```
make        # builds ./cfg
make test   # golden tests plus exhaustive self-tests on every example grammar
```

## Usage

```
cfg check    GRAMMAR            FIRST/FOLLOW sets, LL(1) verdict, finiteness
cfg rank     GRAMMAR 'string'   the index of a string
cfg unrank   GRAMMAR N          the string with index N          (--tree shows the derivation)
cfg list     GRAMMAR K          the first K strings               (--tree shows the derivations)
cfg count    GRAMMAR S          how many strings of each derivation size up to S
cfg selftest GRAMMAR            brute-force enumeration against rank and unrank

cfg shape-rank     GRAMMAR                  the index of the grammar's shape
cfg shape-unrank   N [--names 'S A B']      the N-th shape, printed as a grammar file
cfg shape-list     K                        the first K shapes
cfg shape-count    S                        how many shapes of each size up to S
cfg grammar-rank   GRAMMAR [--alphabet 'a b']           the index of a grammar
cfg grammar-unrank N --alphabet 'a b' [--names 'S A']   the grammar with index N
cfg grammar-list   K --alphabet 'a b'                   the first K grammars
cfg grammar-count  S --alphabet 'a b'                   how many grammars of each size up to S
cfg shape-selftest S [--alphabet 'a b']                 brute force against the tables
```

Indices are arbitrary-precision integers. A 300-digit index decodes in
well under a second.

## Grammar files

Plain BNF:

```
# arithmetic expressions
E  -> T E1
E1 -> '+' T E1 | eps
T  -> F T1
T1 -> '*' F T1 | eps
F  -> '(' E ')' | 'x'
```

EBNF operators:

```
E -> T { '+' T }
T -> F { '*' F }
F -> '(' E ')' | 'x'
```

ISO/IEC 14977 syntax is accepted too, strict or relaxed:

```
(* commas, bracketed comments, multi-word names, semicolons *)
expression    = term, { ( "+" | "-" ), term } ;
number        = decimal digit, { decimal digit } ;
decimal digit = "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" ;
consonant     = letter - vowel ;
blanks        = 5 * " " ;
```

The rules:

- Quoted text (single or double quotes) is a terminal. A bare identifier
  is a nonterminal and must appear on some left-hand side.
- `->`, `::=` or `=` separates the sides. A rule may end with `.` or `;`.
- `|` separates alternatives. `eps` or an empty alternative is the empty
  string.
- `( )` groups, `[ ]` or a postfix `?` is optional, `{ }` or a postfix
  `*` repeats zero or more times, a postfix `+` repeats one or more times.
- The first rule's nonterminal is the start symbol. `#` starts a line
  comment, `(* ... *)` a bracketed one, which may nest.
- From ISO 14977: `,` may separate items, `/` and `!` mean `|`, `(/ /)`
  and `(: :)` are the option and repeat brackets, `3 * x` repeats `x`
  three times, and `x - y` is the exception, see below. Meta-identifiers
  may contain spaces: `syntax rule` and `syntaxrule` are the same name,
  as the standard specifies. Special sequences `? ... ?` are rejected,
  since the standard gives them no meaning.

Input strings are tokenized by longest match against the terminals, and
whitespace between tokens is ignored, so `x+x` and `x + x` are the same
string.

The grammar must be unambiguous. `cfg check` first tests whether it is
LL(1), which guarantees unambiguity and gives a fast predictive parser.
If it is not, for example because it is left-recursive, the grammar is
still accepted provided it survives two checks: no nonterminal may derive
itself, and a brute-force probe over the first couple of thousand
derivation trees must find no two trees with the same string. Strings of
such a grammar are parsed with an Earley parser, which also refuses any
string that turns out to have two derivations. `cfg check` reports which
case applies, and when a grammar is refused it prints the offending
string with both of its derivations.

## How the numbering works

Two problems have to be solved before "the number of a string" means
anything: every string needs a unique structure to rank, and every string
must have finitely many predecessors.

**Unique structure.** An unambiguous grammar gives each string exactly one
derivation tree, so the program ranks trees. LL(1) guarantees unambiguity.

**Finitely many predecessors.** Strings are graded by *derivation size*,
the number of production applications in the tree, and ordered by size
first. Inside one size class the order is: which production the root
uses, in grammar order; then how the remaining size is divided among the
nonterminal children; then the children themselves, left to right.

The counting table behind this is a one-line recurrence. If `N_A(s)` is the
number of trees rooted at `A` with `s` applications, then

```
N_A(s) = sum over productions A -> α  of  C_α(s − 1)
```

where `C_α(t)` counts the ways the nonterminals of `α` can share `t`
applications, a convolution of their own tables. Ranking walks the tree
once, adding up the sizes of everything that comes earlier at each node.
Unranking does the same walk in reverse, subtracting until the number
fits. The table costs `O(|G| · S²)` big-integer multiplications for size
`S`, and memory `O(|G| · S)` big integers.

Derivation size rather than string length was chosen because it makes
ε-productions and unit productions harmless: every production costs one,
so the recurrence is well founded without transforming the grammar. The
price is that the order inside a size class is structural, not
alphabetical, and that it depends on the grammar text. Swapping two
alternatives, or rewriting a loop from BNF to EBNF, can change every
index. Alternatives whose size ranges never overlap are the exception:
the BNF and EBNF expression grammars above produce identical numbers.

**EBNF** is desugared when the grammar is loaded. Each operator becomes a
generated nonterminal named after its rule, with the empty alternative
first so that at equal size fewer iterations come before more:

```
( a | b )      E.g1 -> a | b
[ a ]  a?      E.o1 -> eps | a
{ a }  a*      E.r1 -> eps | a E.r1
a+             E.p1 -> a E.r1
```

`cfg check` prints the generated rules, and `--tree` shows them in
derivations, so the numbering is always explainable.

**Exceptions.** `factor - exception` denotes the strings of the factor
that are not strings of the exception. A set difference of two
context-free languages is not context-free in general, but the standard
requires the exception to be expressible without meta-identifiers, which
makes it regular, and a context-free language minus a regular one is
context-free. The loader builds a finite automaton for the exception,
complements it, and intersects the factor's sub-grammar with it: every
nonterminal `B` of that sub-grammar becomes `B[p,q]`, a derivation whose
yield drives the automaton from state `p` to state `q`. Because the
automaton is deterministic, each original tree corresponds to exactly
one product tree, so the product is unambiguous whenever the original
is and counting works unchanged. The product can fail to be LL(1) even
when the original was; `cfg check` then says so and names the rule. In
practice character classes minus a few characters, as in
`letter - vowel`, and repetition minus the empty sequence, as in
`{ "A" }-`, stay LL(1). An exception that is itself recursive beyond
tail recursion, such as a nested bracket language, is rejected as not
regular.

**Beyond LL(1).** Unambiguity is undecidable in general, so for a grammar
that fails the LL(1) test the program cannot prove the numbering is a
bijection. It does the next best thing. A grammar in which some
nonterminal derives itself through unit and nullable productions gives
every string infinitely many derivations and is refused outright. Any
other grammar is probed: derivation trees are enumerated by brute force,
about two thousand of them, and if two have the same yield the grammar is
refused with that string and both trees as the witness. Practical
ambiguities such as `E -> E '+' E` or the dangling else fall within the
first few hundred trees. A grammar that passes is parsed with Earley's
algorithm, which keeps every way an item was reached and so notices when a
string has more than one derivation, refusing it. The weak spot is
`unrank`, which cannot notice ambiguity; the self-test closes it for small
sizes by checking that all enumerated trees have distinct strings.

**Finite languages** are detected by a cycle check on the grammar graph.
For them the program knows the total number of strings and rejects larger
indices instead of searching forever. For a finite language that is not
LL(1) the ambiguity probe is exhaustive, so its verdict is a proof.

## Enumerating grammars

The same idea applies one level up: the grammars themselves can be
numbered. The index of a grammar must not depend on what its nonterminals
are called or on the order of its rules, so the thing that is ranked is the
grammar's *shape*: read the rules from the start symbol in the order their
nonterminals are first mentioned, alternatives in order, symbols left to
right, and replace the i-th new nonterminal by `Ni` and the j-th new
terminal by `tj`. This is the de Bruijn trick again, applied to grammars
instead of lambda terms.

```
$ ./cfg shape-list 12
0       N0 -> eps
1       N0 -> 't0'
2       N0 -> N0
3       N0 -> eps | eps
4       N0 -> 't0' 't0'
5       N0 -> 't0' 't1'
6       N0 -> 't0' N0
7       N0 -> 't0' | eps
8       N0 -> N0 't0'
9       N0 -> N0 N0
10      N0 -> N0 | eps
11      N0 -> N1 ; N1 -> eps

$ ./cfg grammar-rank grammars/expr.txt
145898704048748911158

$ ./cfg grammar-unrank 145898704048748911158 --alphabet '( ) * + x' --names 'E T E1 F T1'
E -> T E1
T -> F T1
E1 -> '+' T E1 | eps
F -> '(' E ')' | 'x'
T1 -> '*' F T1 | eps
```

A shape is counted as a sequence of actions (write an old or a new
terminal, write an old or a new nonterminal, end the alternative, end the
rule); its size is the number of actions. Ranking is a walk along the
sequence that adds up, for every position, the completions of every action
that would have come earlier. The terminal actions interleave freely with
the rest, so the number of completions is a convolution of r-Bell numbers
(terminal subsequences) with a three-parameter recurrence for the
nonterminal subsequences, whose levels are streamed and never stored
whole. The 471-digit index of the standard's own grammar of EBNF,
`grammars/iso-14977.ebnf`, decodes in half a second and about 20 MB.
Unreachable rules are not part of the shape, and the program says so when
it drops them.

A grammar over an alphabet is a shape plus a *spelling*: which real
terminal appeared first, which second, and so on. The spelling is a
k-permutation of the alphabet, ranked in the factorial number system, and
the grammars of one size are ordered shape-major, so consecutive indices
are usually relabelings of the same structure. The alphabet has to be
given, since the index over `{a, b}` differs from the index over
`{a, b, c}`; `grammar-rank` defaults to the grammar's own terminals in
sorted order. Nonterminal names are never part of the index; `--names`
only chooses what `unrank` prints. The plan with the counting recurrence
is in [docs/grammar-enumeration.md](docs/grammar-enumeration.md).

Shapes make the analysis a survey tool: `shape-unrank` every shape up to
some size and run `check` on it to count how many are LL(1), how many are
ambiguous, how many describe finite languages.

## Testing

`make test` runs two kinds of tests.

The self-test, `cfg selftest GRAMMAR`, enumerates every derivation tree up
to a few thousand strings with an independent brute-force generator and
checks, for each one, that the table count agrees, that `rank` returns its
position, that `unrank` of that position returns the same tree, and that
printing and re-parsing the string gives the tree back. It then
round-trips indices 0 to 19999 and a few 13- to 50-digit numbers.

`tests/arithmetic.sh` holds golden values for the ISO EBNF arithmetic
grammar: counts, first strings, specific ranks and unranks, round trips,
the tree view and rejected inputs. `tests/iso.sh` covers the ISO syntax:
the strict and relaxed arithmetic grammars must agree on every index, the
standard's repetition examples, exceptions, the standard's own grammar of
EBNF from clause 8.1, alternative symbols, and a set of grammars that
must be rejected, including the standard's ambiguous examples `cc` and
`gg`. `tests/earley.sh` covers the non-LL(1) path: a left-recursive
expression grammar, which accepts the same strings as `expr.txt` under a
different numbering, odd palindromes, and the grammars that must be
refused with their witnesses. `tests/shapes.sh` covers the grammar
enumeration: counts, the first shapes and grammars, indices of the example
grammars, invariance under renaming and reordering rules, round trips
through `unrank` and a file, and the rejected inputs. `cfg shape-selftest`
enumerates every shape up to a given size straight from the action
definition and checks the table counts, every rank and unrank, and the
grammar index of every spelling over a small alphabet.

## Limitations

- For a grammar that is not LL(1), unambiguity is checked only up to the
  probe's bound and at `rank` time. An ambiguity that first appears in
  large strings would make `unrank` return the same string for two
  indices without warning.
- The Earley parser is cubic in the length of the input in the worst case
  and uses linear scans inside item sets, so strings of thousands of
  tokens are slow.
- The numbering is a property of the grammar text, not of the language.
- Identifiers cannot contain a prime, so write `E1` rather than `E'`.
- Output separates every token with a space, which makes `890` print as
  `8 9 0` when digits are separate terminals. If some terminal contains
  whitespace, tokens are printed without separators instead.
- Two adjacent loose-syntax symbols whose names concatenate to a defined
  nonterminal are read as that nonterminal, a consequence of accepting
  spaces inside ISO meta-identifiers.
- Decoding builds the full counting table up to the size of the result, so
  indices with thousands of digits take seconds and tens of megabytes.
- Grammars that differ only in the order of alternatives have different
  shapes, as they have different numberings of their strings.

## Where this kind of thing is used

Ranking and unranking is the engine behind uniform random generation of
syntax trees (grammar-based fuzzing, property-based testing), optimal
compression of structured data, format-preserving encryption, dense Gödel
numberings, and exhaustive enumeration of programs in synthesis and
superoptimization. The classic references are Hickey and Cohen,
*Uniform random generation of strings in a context-free language* (1983),
Mäkinen, *Ranking and unranking left Szilard languages* (1997), and
Goldberg and Sipser, *Compression and ranking* (1985). The lambda-term
origin of this project follows Grygiel and Lescanne,
*Counting and generating lambda terms* (2013).

## License

MIT, see [LICENSE](LICENSE).

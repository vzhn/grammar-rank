# cfg-order

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

ISO EBNF punctuation is accepted too:

```
expression = term { ("+" | "-") term } .
number     = digit { digit } .
digit      = "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" .
```

The rules:

- Quoted text (single or double quotes) is a terminal. A bare identifier
  is a nonterminal and must appear on some left-hand side.
- `->`, `::=` or `=` separates the sides. A rule may end with `.` or `;`.
- `|` separates alternatives. `eps` or an empty alternative is the empty
  string.
- `( )` groups, `[ ]` or a postfix `?` is optional, `{ }` or a postfix
  `*` repeats zero or more times, a postfix `+` repeats one or more times.
- The first rule's nonterminal is the start symbol. `#` starts a comment.

Input strings are tokenized by longest match against the terminals, and
whitespace between tokens is ignored, so `x+x` and `x + x` are the same
string.

The grammar must be LL(1). `cfg check` tells you whether it is and, if
not, which alternatives conflict on which token. Left recursion and
ambiguity both fail that test. LL(1) is stronger than strictly necessary,
since the method only needs unambiguity, but it is easy to check and it
gives a fast parser for free.

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

**Finite languages** are detected by a cycle check on the grammar graph.
For them the program knows the total number of strings and rejects larger
indices instead of searching forever.

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
the tree view and rejected inputs.

## Limitations

- Only LL(1) grammars. An unambiguous grammar that is not LL(1) would
  work with a different parser, but the program does not have one.
- The numbering is a property of the grammar text, not of the language.
- Identifiers cannot contain a prime, so write `E1` rather than `E'`.
- Output separates every token with a space, which makes `890` print as
  `8 9 0` when digits are separate terminals.
- Decoding builds the full counting table up to the size of the result, so
  indices with thousands of digits take seconds and tens of megabytes.

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

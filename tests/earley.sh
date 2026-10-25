#!/bin/sh
# Tests for grammars that are not LL(1): Earley parsing, the ambiguity probe
# and the self-derivation check.
# Usage: sh tests/earley.sh [path-to-cfg]
CFG=${1:-./cfg}
pass=0; fail=0
ok()   { pass=$((pass + 1)); }
bad()  { fail=$((fail + 1)); echo "FAIL: $*"; }
eq()   { if [ "$2" = "$3" ]; then ok; else bad "$1: expected [$2], got [$3]"; fi; }
has()  { case "$2" in *"$3"*) ok ;; *) bad "$1: output lacks [$3]";; esac; }
accepts() { if "$CFG" rank "$2" "$3" >/dev/null 2>&1; then ok; else bad "$1: '$3' rejected"; fi; }
rejects() { if "$CFG" rank "$2" "$3" >/dev/null 2>&1; then bad "$1: '$3' accepted"; else ok; fi; }
joined()  { "$CFG" list "$1" "$2" | cut -f2 | tr -d ' ' | tr '\n' ' ' | sed 's/ $//'; }

# --- left-recursive expressions ------------------------------------------------
L=grammars/leftrec.txt
chk=$("$CFG" check $L 2>&1)
has "leftrec not LL(1)" "$chk" "LL(1): no (E: alternatives 1 and 2 both predicted by ()"
has "leftrec Earley" "$chk" "parser: Earley"
has "leftrec probe" "$chk" "ambiguity probe: none found among"
eq "leftrec first strings" "x x*x x+x (x) x*x*x x+x*x x*x+x x*(x)" "$(joined $L 8)"
eq "leftrec rank" "25" "$("$CFG" rank $L 'x+x*(x)')"
eq "leftrec tree" "[E [E [T [F 'x']]] '+' [T [T [F 'x']] '*' [F '(' [E [T [F 'x']]] ')']]]" \
   "$("$CFG" rank $L 'x+x*(x)' --tree | head -1)"
eq "leftrec rank x+x+x" "11" "$("$CFG" rank $L 'x+x+x')"
for s in 'x' '(x)' 'x+x+x+x' '((x))*x' 'x*(x+x)*x'; do accepts leftrec $L "$s"; done
for s in '' 'x+' '(x))' 'x x' '+x' '()'; do rejects leftrec $L "$s"; done
for n in 0 1 7 100 12345 99999999 123456789012345678901234567890; do
    s=$("$CFG" unrank $L "$n"); eq "leftrec rank(unrank($n))" "$n" "$("$CFG" rank $L "$s")"
done
has "leftrec selftest" "$("$CFG" selftest $L 2>&1 | tail -1)" "selftest OK"

# same language as expr.txt, different numbering
X=grammars/expr.txt
for s in 'x' 'x+x' 'x*x' 'x+x*(x)' '(x+x)*x'; do
    eq "same language: $s" "$("$CFG" rank $X "$s" >/dev/null 2>&1 && echo yes)" "$("$CFG" rank $L "$s" >/dev/null 2>&1 && echo yes)"
done
eq "expr index of x+x*(x)" "34" "$("$CFG" rank $X 'x+x*(x)')"

# --- palindromes: unambiguous, not LL(k) --------------------------------------
P=grammars/palindrome.txt
has "palindrome Earley" "$("$CFG" check $P)" "parser: Earley"
eq "palindrome strings" "a aaa aaaaa aaaaaaa" "$(joined $P 4)"
eq "palindrome rank" "2" "$("$CFG" rank $P 'aaaaa')"
eq "palindrome tree" "[S 'a' [S 'a' [S 'a'] 'a'] 'a']" "$("$CFG" rank $P 'aaaaa' --tree | head -1)"
for s in 'aa' 'aaaa' ''; do rejects palindrome $P "$s"; done
has "palindrome selftest" "$("$CFG" selftest $P 2>&1 | tail -1)" "selftest OK"

# --- refused grammars ----------------------------------------------------------
A=grammars/ambig.txt
chk=$("$CFG" check $A 2>&1)
has "ambig witness" "$chk" "REJECTED: ambiguous, 'x + x + x' has two derivations:"
has "ambig tree 1" "$chk" "[E [E 'x'] '+' [E [E 'x'] '+' [E 'x']]]"
has "ambig tree 2" "$chk" "[E [E [E 'x'] '+' [E 'x']] '+' [E 'x']]"
if "$CFG" check $A >/dev/null 2>&1; then bad "ambig: check exit 0"; else ok; fi
has "ambig rank refused" "$("$CFG" rank $A 'x' 2>&1)" "ambiguous grammar"
has "ambig unrank refused" "$("$CFG" unrank $A 0 2>&1)" "ambiguous grammar"

D=grammars/dangling.txt
has "dangling witness" "$("$CFG" check $D 2>&1)" "REJECTED: ambiguous, 'if if x else x' has two derivations:"
if "$CFG" list $D 3 >/dev/null 2>&1; then bad "dangling: list succeeded"; else ok; fi

C=grammars/cycle.txt
has "cycle message" "$("$CFG" check $C 2>&1)" "REJECTED: A derives itself"
has "cycle rank refused" "$("$CFG" rank $C 'x' 2>&1)" "derives itself"

tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT
printf 'S -> A B\nA -> eps | S\nB -> eps\n' > "$tmp/nullcycle.txt"        # S => A B => S B => S
has "nullable cycle" "$("$CFG" check "$tmp/nullcycle.txt" 2>&1)" "derives itself"
printf 'S -> S S | x\nx -> eps\n' > "$tmp/epsamb.txt"                      # infinitely many empty derivations
if "$CFG" check "$tmp/epsamb.txt" >/dev/null 2>&1; then bad "epsamb accepted"; else ok; fi

# --- a finite non-LL(1) language is checked exhaustively ------------------------
printf 'S -> A | B\nA -> "x"\nB -> "x" "y"\n' > "$tmp/finite.txt"
chk=$("$CFG" check "$tmp/finite.txt" 2>&1)
has "finite exhaustive" "$chk" "all 2 derivations of the finite language were checked"
eq "finite strings" "x xy" "$(joined "$tmp/finite.txt" 5)"

echo "earley: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

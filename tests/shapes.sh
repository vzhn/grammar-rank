#!/bin/sh
# Golden tests for the enumeration of grammars: shapes, terminal spellings,
# invariance under nonterminal names and rule order, round trips, errors.
# Usage: sh tests/shapes.sh [path-to-cfg]
CFG=${1:-./cfg}
pass=0; fail=0
ok()   { pass=$((pass + 1)); }
bad()  { fail=$((fail + 1)); echo "FAIL: $*"; }
eq()   { if [ "$2" = "$3" ]; then ok; else bad "$1: expected [$2], got [$3]"; fi; }
has()  { case "$2" in *"$3"*) ok ;; *) bad "$1: output lacks [$3]";; esac; }
fails() { if "$CFG" "$@" >/dev/null 2>&1; then bad "$*: accepted"; else ok; fi; }
tmp=$(mktemp); trap 'rm -f "$tmp"' EXIT

# --- counts --------------------------------------------------------------------
eq "shape counts" "0 1 3 11 48 240 1336 8128 53376" "$("$CFG" shape-count 8 | tail -n +2 | cut -f2 | tr '\n' ' ' | sed 's/ $//')"
eq "shape offsets" "0 0 1 4 15 63 303 1639 9767" "$("$CFG" shape-count 8 | tail -n +2 | cut -f3 | tr '\n' ' ' | sed 's/ $//')"
eq "grammar counts ab" "0 1 4 17 78 389 2101 12168 74768" "$("$CFG" grammar-count 8 --alphabet 'a b' | tail -n +2 | cut -f3 | tr '\n' ' ' | sed 's/ $//')"
eq "grammar count a" "6	1336	785	216" "$("$CFG" grammar-count 6 --alphabet 'a' | sed -n 8p)"

# --- the first shapes and grammars -----------------------------------------------
eq "first shapes" "N0 -> eps|N0 -> 't0'|N0 -> N0|N0 -> eps | eps|N0 -> 't0' 't0'|N0 -> 't0' 't1'|N0 -> 't0' N0|N0 -> 't0' | eps|N0 -> N0 't0'|N0 -> N0 N0|N0 -> N0 | eps|N0 -> N1 ; N1 -> eps" \
   "$("$CFG" shape-list 12 | cut -f2 | paste -sd '|')"
eq "first grammars" "N0 -> eps|N0 -> 'a'|N0 -> 'b'|N0 -> N0|N0 -> eps | eps|N0 -> 'a' 'a'|N0 -> 'b' 'b'|N0 -> 'a' 'b'|N0 -> 'b' 'a'|N0 -> 'a' N0|N0 -> 'b' N0|N0 -> 'a' | eps|N0 -> 'b' | eps|N0 -> N0 'a'" \
   "$("$CFG" grammar-list 14 --alphabet 'a b' | cut -f2 | paste -sd '|')"
eq "names" "1	S -> 't0'" "$("$CFG" shape-list 2 --names 'S' | sed -n 2p)"

# --- example grammars ------------------------------------------------------------
eq "expr shape" "2221619179141381661" "$("$CFG" shape-rank grammars/expr.txt)"
eq "expr grammar" "145898704048748911158" "$("$CFG" grammar-rank grammars/expr.txt)"
eq "expr grammar, explicit alphabet" "145898704048748911158" "$("$CFG" grammar-rank grammars/expr.txt --alphabet '( ) * + x')"
eq "expr grammar, larger alphabet" "4728058312904867055411" "$("$CFG" grammar-rank grammars/expr.txt --alphabet 'x + * ( ) y z')"
eq "dyck shape" "590" "$("$CFG" shape-rank grammars/dyck.txt)"
eq "dyck grammar" "857" "$("$CFG" grammar-rank grammars/dyck.txt)"
eq "leftrec grammar" "6055594755967514" "$("$CFG" grammar-rank grammars/leftrec.txt)"
eq "ambig grammar" "1363" "$("$CFG" grammar-rank grammars/ambig.txt)"
eq "arithmetic.ebnf grammar" "94678111051175772362206102628364499116614555142596945560127712496917686112" "$("$CFG" grammar-rank grammars/arithmetic.ebnf)"
eq "expr back from its index" "E -> T E1|T -> F T1|E1 -> '+' T E1 | eps|F -> '(' E ')' | 'x'|T1 -> '*' F T1 | eps" \
   "$("$CFG" grammar-unrank 145898704048748911158 --alphabet '( ) * + x' --names 'E T E1 F T1' | paste -sd '|')"

# --- invariance: names, rule order, unreachable rules ----------------------------
cat > "$tmp" <<'G'
Expr -> Term ExprRest
Factor -> "(" Expr ")" | "x"
TermRest -> "*" Factor TermRest | eps
ExprRest -> "+" Term ExprRest | eps
Term  -> Factor TermRest
G
eq "renamed and reordered" "145898704048748911158" "$("$CFG" grammar-rank "$tmp")"
printf 'Unused -> "q" Unused\n' >> "$tmp"
eq "unreachable rule dropped" "cfg: 1 unreachable nonterminal is not part of the shape|145898704048748911158" "$("$CFG" grammar-rank "$tmp" 2>&1 | paste -sd '|')"
eq "unreachable rule dropped, shape" "2221619179141381661" "$("$CFG" shape-rank "$tmp" 2>/dev/null)"
printf 'S -> S "x" | eps\n' > "$tmp"
printf 'S -> S "y" | eps\n' > "$tmp.2"
eq "terminal names do not matter alone" "$("$CFG" grammar-rank "$tmp")" "$("$CFG" grammar-rank "$tmp.2")"
rm -f "$tmp.2"

# --- unrank and round trips ------------------------------------------------------
eq "shape 1234567890" "N0 -> N1 't0'|N1 -> 't1' N2 N2|N2 -> N3 't0' 't2' 't2'|N3 -> eps" "$("$CFG" shape-unrank 1234567890 | paste -sd '|')"
eq "grammar 1234567890" "S -> 'b' 'a' 'c' 'b' | A 'c' 'b' | eps | 'a'|A -> eps" \
   "$("$CFG" grammar-unrank 1234567890 --alphabet 'a b c' --names 'S A B' | paste -sd '|')"
for n in 0 1 2 3 11 12 63 302 1000 999999 123456789012345678901234567890; do
    "$CFG" shape-unrank "$n" > "$tmp"
    eq "shape round trip $n" "$n" "$("$CFG" shape-rank "$tmp")"
    "$CFG" grammar-unrank "$n" --alphabet 'x y z' > "$tmp"
    eq "grammar round trip $n" "$n" "$("$CFG" grammar-rank "$tmp" --alphabet 'x y z')"
done
"$CFG" grammar-unrank 31415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679 --alphabet "'+' - * / ( ) x" > "$tmp"
eq "100-digit round trip" "31415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679" \
   "$("$CFG" grammar-rank "$tmp" --alphabet "'+' - * / ( ) x")"

# --- errors ----------------------------------------------------------------------
fails grammar-list 3
fails grammar-unrank 5 --alphabet 'a a'
fails grammar-rank grammars/expr.txt --alphabet 'x y'
fails shape-count abc
fails shape-unrank -1
has "alphabet error" "$("$CFG" grammar-rank grammars/expr.txt --alphabet 'x y' 2>&1)" "terminal '+' is not in the alphabet"
has "duplicate error" "$("$CFG" grammar-unrank 5 --alphabet 'a a' 2>&1)" "'a' is listed twice"

# --- self-test -------------------------------------------------------------------
has "selftest" "$("$CFG" shape-selftest 6 --alphabet 'a b c' 2>&1 | tail -1)" "selftest OK"

echo "shapes: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

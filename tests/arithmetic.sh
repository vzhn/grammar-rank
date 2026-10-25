#!/bin/sh
# Golden tests for grammars/arithmetic.ebnf (ISO EBNF syntax).
# Usage: sh tests/arithmetic.sh [path-to-cfg]
CFG=${1:-./cfg}
G=grammars/arithmetic.ebnf
pass=0; fail=0

ok()   { pass=$((pass + 1)); }
bad()  { fail=$((fail + 1)); echo "FAIL: $*"; }
strip(){ printf '%s' "$1" | tr -d ' '; }

# a test case is: description, expected, actual
eq() {
    if [ "$2" = "$3" ]; then ok; else bad "$1: expected [$2], got [$3]"; fi
}

# --- grammar analysis -------------------------------------------------------
check=$("$CFG" check "$G" 2>&1)
case "$check" in *"LL(1): yes"*)          ok ;; *) bad "check: not LL(1)";;         esac
case "$check" in *"language: infinite"*)  ok ;; *) bad "check: not infinite";;      esac
case "$check" in *"expression.r1: (generated)"*) ok ;; *) bad "check: missing generated rule";; esac

# --- counts: an n-digit number has derivation size 6 + 2n ---------------------
eq "count size 8"  "8	10	0"      "$("$CFG" count "$G" 12 | sed -n '/^8\t/p')"
eq "count size 10" "10	100	10"    "$("$CFG" count "$G" 12 | sed -n '/^10\t/p')"
eq "count size 12" "12	1000	110" "$("$CFG" count "$G" 12 | sed -n '/^12\t/p')"
eq "count size 9 is empty" "9	0	10" "$("$CFG" count "$G" 12 | sed -n '/^9\t/p')"

# --- first strings ----------------------------------------------------------
eq "list" "0 1 2 3 4 5 6 7 8 9 00 01" \
   "$("$CFG" list "$G" 12 | cut -f2 | tr -d ' ' | tr '\n' ' ' | sed 's/ $//')"

# --- rank golden values -----------------------------------------------------
while IFS='|' read -r s n; do
    eq "rank $s" "$n" "$("$CFG" rank "$G" "$s")"
done <<'TABLE'
1|1
12|22
1+2|11442
(7)|1117
1+2*3|12188373
12/4-3|132498033
2025-10-25|14548835492785
(1+2)*(3-4)/5|37016340751379736775
TABLE

# --- unrank golden values ---------------------------------------------------
while IFS='|' read -r n s; do
    eq "unrank $n" "$s" "$(strip "$("$CFG" unrank "$G" "$n")")"
done <<'TABLE'
0|0
1000|890
1000000|819380
123456789012345678901234567890|32595158265121918246480401470
TABLE

# --- round trips ------------------------------------------------------------
for s in '0' '42' '1+2*3' '(1+2)*(3-4)/5' '((((0))))' '9-9-9-9' '1*2/3*4'; do
    n=$("$CFG" rank "$G" "$s")
    eq "unrank(rank($s))" "$s" "$(strip "$("$CFG" unrank "$G" "$n")")"
done
for n in 0 1 9 10 999 123456 99999999999999999999 31415926535897932384626433832795028841971; do
    s=$("$CFG" unrank "$G" "$n")
    eq "rank(unrank($n))" "$n" "$("$CFG" rank "$G" "$s")"
done

# --- whitespace in input is ignored -----------------------------------------
eq "spaces ignored" "$("$CFG" rank "$G" '1+2*3')" "$("$CFG" rank "$G" ' 1 + 2 * 3 ')"

# --- tree view ----------------------------------------------------------------
eq "tree of 7" "[expression [term [factor [number [digit '7'] [number.r1 eps]]] [term.r1 eps]] [expression.r1 eps]]" \
   "$("$CFG" unrank "$G" 7 --tree)"

# --- rejected input -----------------------------------------------------------
if "$CFG" rank "$G" '1+'  >/dev/null 2>&1; then bad "'1+' accepted";  else ok; fi
if "$CFG" rank "$G" 'a'   >/dev/null 2>&1; then bad "'a' accepted";   else ok; fi
if "$CFG" rank "$G" '()'  >/dev/null 2>&1; then bad "'()' accepted";  else ok; fi
if "$CFG" rank "$G" '1+*2' >/dev/null 2>&1; then bad "'1+*2' accepted"; else ok; fi
if "$CFG" rank "$G" '+1'  >/dev/null 2>&1; then bad "'+1' accepted";  else ok; fi
eq "'1 2' is the number 12" "$("$CFG" rank "$G" '12')" "$("$CFG" rank "$G" '1 2')"
if "$CFG" unrank "$G" -1  >/dev/null 2>&1; then bad "unrank -1 accepted"; else ok; fi

# --- exhaustive self-test -----------------------------------------------------
case "$("$CFG" selftest "$G" 2>&1 | tail -1)" in "selftest OK") ok ;; *) bad "selftest";; esac

echo "arithmetic.ebnf: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

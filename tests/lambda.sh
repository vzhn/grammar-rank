#!/bin/sh
# Golden tests for grammars/lambda.ebnf (lambda calculus, de Bruijn indices).
# Usage: sh tests/lambda.sh [path-to-cfg]
CFG=${1:-./cfg}
G=grammars/lambda.ebnf
pass=0; fail=0

ok()   { pass=$((pass + 1)); }
bad()  { fail=$((fail + 1)); echo "FAIL: $*"; }
strip(){ printf '%s' "$1" | tr -d ' '; }

eq() {
    if [ "$2" = "$3" ]; then ok; else bad "$1: expected [$2], got [$3]"; fi
}

# --- grammar analysis -------------------------------------------------------
check=$("$CFG" check "$G" 2>&1)
case "$check" in *"LL(1): yes"*)         ok ;; *) bad "check: not LL(1)";;    esac
case "$check" in *"language: infinite"*) ok ;; *) bad "check: not infinite";; esac

# --- counts -----------------------------------------------------------------
eq "count size 5"  "5	10	0"       "$("$CFG" count "$G" 12 | sed -n '/^5\t/p')"
eq "count size 6"  "6	0	10"       "$("$CFG" count "$G" 12 | sed -n '/^6\t/p')"
eq "count size 7"  "7	10	10"      "$("$CFG" count "$G" 12 | sed -n '/^7\t/p')"
eq "count size 8"  "8	100	20"     "$("$CFG" count "$G" 12 | sed -n '/^8\t/p')"
eq "count size 11" "11	1030	240"  "$("$CFG" count "$G" 12 | sed -n '/^11\t/p')"
eq "count size 12" "12	400	1270"  "$("$CFG" count "$G" 12 | sed -n '/^12\t/p')"

# --- first strings: variables, then abstractions, then applications ----------
eq "list" '0 1 2 3 4 5 6 7 8 9 \0 \1 \2 \3 \4 \5 \6 \7 \8 \9 00 01' \
   "$("$CFG" list "$G" 22 | cut -f2 | tr -d ' ' | tr '\n' ' ' | sed 's/ $//')"

# --- rank golden values: the classic combinators -----------------------------
while IFS='|' read -r s n; do
    eq "rank $s" "$n" "$("$CFG" rank "$G" "$s")"
done <<'TABLE'
\0|10
\\1|121
(0)|130
\ 0 (1 2)|33362
\\1 (1 (1 0))|69155960
\\\2 0 (1 0)|18446400
(\0 0) (\0 0)|209530020
\\\\3 1 (2 1 0)|917889330
TABLE

# --- unrank golden values ---------------------------------------------------
while IFS='|' read -r n s; do
    eq "unrank $n" "$s" "$(strip "$("$CFG" unrank "$G" "$n")")"
done <<'TABLE'
0|0
2026|\336
1000000|463330
314159265358979323846264338327950288419716939937510|50((49870))889(92)8163(92(\\0011787)972)379577410
TABLE

# --- round trips ------------------------------------------------------------
for s in '0' '\0' '\\1' '\\\2 0 (1 0)' '(\0 0) (\0 0)' '((((0))))' '0 1 2 3' '\\\\\\\\\\9'; do
    n=$("$CFG" rank "$G" "$s")
    eq "unrank(rank($s))" "$(strip "$s")" "$(strip "$("$CFG" unrank "$G" "$n")")"
done
for n in 0 1 9 10 999 123456 99999999999999999999 31415926535897932384626433832795028841971; do
    s=$("$CFG" unrank "$G" "$n")
    eq "rank(unrank($n))" "$n" "$("$CFG" rank "$G" "$s")"
done

# --- indices are single digits: 12 is the application of 1 to 2 --------------
eq "12 is 1 2" "$("$CFG" rank "$G" '1 2')" "$("$CFG" rank "$G" '12')"

# --- a lambda in argument position needs parentheses ------------------------
case "$("$CFG" rank "$G" '(\0 0) \0 0' 2>&1)" in
    *"syntax error"*) ok ;; *) bad "unparenthesized lambda argument accepted";;
esac

echo "tests/lambda.sh: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

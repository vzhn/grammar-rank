#!/bin/sh
# Tests for ISO/IEC 14977 syntax: comments, commas, alternative symbols,
# multi-word meta-identifiers, integer repetition and exceptions.
# Usage: sh tests/iso.sh [path-to-cfg]
CFG=${1:-./cfg}
pass=0; fail=0
ok()   { pass=$((pass + 1)); }
bad()  { fail=$((fail + 1)); echo "FAIL: $*"; }
strip(){ printf '%s' "$1" | tr -d ' '; }
eq()   { if [ "$2" = "$3" ]; then ok; else bad "$1: expected [$2], got [$3]"; fi; }
has()  { case "$2" in *"$3"*) ok ;; *) bad "$1: output lacks [$3]";; esac; }
rejects() { if "$CFG" check "$2" >/dev/null 2>&1; then bad "$1: grammar accepted"; else ok; fi; }

tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT

# --- strict ISO arithmetic numbers strings exactly like the loose file ------
A=grammars/arithmetic.ebnf; I=grammars/iso-arith.ebnf
has "iso-arith LL(1)" "$("$CFG" check $I)" "LL(1): yes"
for s in '7' '12' '1+2*3' '(1+2)*(3-4)/5' '2025-10-25' '0/0/0'; do
    eq "iso-arith rank $s" "$("$CFG" rank $A "$s")" "$("$CFG" rank $I "$s")"
done
eq "iso-arith counts" "$("$CFG" count $A 20 | md5sum)" "$("$CFG" count $I 20 | md5sum)"
has "iso-arith generated names" "$("$CFG" check $I)" "expression.g1: (generated)"

# --- exceptions and integer repetition ----------------------------------------
E=grammars/exceptions.ebnf
chk=$("$CFG" check $E 2>&1)
has "exceptions LL(1)" "$chk" "LL(1): yes"
case "$chk" in *UNREACHABLE*|*UNPRODUCTIVE*) bad "exceptions: spurious warning";; *) ok;; esac
eq "letter - vowel has 21 alternatives" "21" "$(printf '%s\n' "$chk" | grep -c '^  [0-9]*\. consonant\.x1 ->')"
has "vowel removed" "$(printf '%s\n' "$chk" | grep '^  [0-9]*\. consonant\.x1 ->' | tr -d '\n')" "'B'"
case "$(printf '%s\n' "$chk" | grep '^  [0-9]*\. consonant\.x1 ->')" in *"'A'"*|*"'E'"*|*"'U'"*) bad "vowel kept";; *) ok;; esac
eq "{A}- is one-or-more" "1. word.x1 -> 'A' word.r1[1,1]" "$(printf '%s\n' "$chk" | grep -o '1\. word\.x1 -> .*' | sed 's/ *predict.*//')"
eq "list" "aa:A dd:D consonant:B consonant:C consonant:D consonant:F" \
   "$("$CFG" list $E 6 | cut -f2 | tr -d ' ' | tr '\n' ' ' | sed 's/ $//')"
eq "rank consonant:B" "2" "$("$CFG" rank $E 'consonant:B')"
eq "rank word:AAE" "27" "$("$CFG" rank $E 'word:AAE')"
eq "rank bb:AAAB" "25" "$("$CFG" rank $E 'bb:AAAB')"
eq "whitespace ignored where no terminal is blank" "$("$CFG" rank $I '1+2')" "$("$CFG" rank $I ' 1 + 2 ')"
eq "whitespace terminal" "793" "$("$CFG" rank $E 'line:  A 0')"
eq "whitespace terminal round trip" "line:  A 0" "$("$CFG" unrank $E 793)"
for s in 'word:E' 'consonant:A' 'consonant:E' 'line:  0' 'line: A' 'bb:AAB' 'aa:AA'; do
    if "$CFG" rank $E "$s" >/dev/null 2>&1; then bad "'$s' accepted"; else ok; fi
done
for n in 0 1 2 100 12345 99999999999; do
    s=$("$CFG" unrank $E "$n"); eq "rank(unrank($n))" "$n" "$("$CFG" rank $E "$s")"
done
has "exceptions selftest" "$("$CFG" selftest $E 2>&1 | tail -1)" "selftest OK"

# --- the standard's own syntax, clause 8.1 ----------------------------------
S=grammars/iso-14977.ebnf
chk=$("$CFG" check $S 2>&1)
has "iso-14977 LL(1)" "$chk" "LL(1): yes"
case "$chk" in *UNREACHABLE*|*UNPRODUCTIVE*) bad "iso-14977: spurious warning";; *) ok;; esac
has "multi-word names" "$chk" "definition separator symbol:"
eq "rank an EBNF rule" "2083272019248199" "$("$CFG" rank $S "a = 'b' , c | 2 * d - 'e' ;")"
eq "spaces in identifiers are not significant" "$("$CFG" rank $S "ab = 'c' ;")" "$("$CFG" rank $S "a b = 'c' ;")"
eq "unrank" "aecc='-10*)[bd-20';" "$(strip "$("$CFG" unrank $S 12345678901234567890)")"
eq "first strings" "a=; b=; c=; d=; e=; a=a;" "$("$CFG" list $S 6 | cut -f2 | tr -d ' ' | tr '\n' ' ' | sed 's/ $//')"
for s in "a = ;" "a = 'b' ;" "x = a ;" "a = 'b" "a = 2 ;" "a = 'b' ; ;"; do
    case "$s" in "a = ;"|"a = 'b' ;") want=accept;; *) want=reject;; esac
    if "$CFG" rank $S "$s" >/dev/null 2>&1; then got=accept; else got=reject; fi
    eq "iso-14977 '$s'" "$want" "$got"
done
has "iso-14977 selftest" "$("$CFG" selftest $S 2>&1 | tail -1)" "selftest OK"

# --- alternative representations (table 2) and terminators -------------------
printf '(* alternative symbols *)\nS = (/ "a" /), (: "b" :), "c" / "d" ! "e" .\n' > "$tmp/alt.ebnf"
has "alt symbols LL(1)" "$("$CFG" check "$tmp/alt.ebnf")" "LL(1): yes"
eq "alt symbols list" "d e c ac bc" "$("$CFG" list "$tmp/alt.ebnf" 5 | cut -f2 | tr -d ' ' | tr '\n' ' ' | sed 's/ $//')"
printf 'S = (* nested (* comment *) here *) "a" ;\n' > "$tmp/nested.ebnf"
eq "nested comment" "a" "$("$CFG" unrank "$tmp/nested.ebnf" 0)"
printf 'S -> "x" S | "y"\n' > "$tmp/loose.txt"
eq "loose syntax still works" "y xy xxy" "$("$CFG" list "$tmp/loose.txt" 3 | cut -f2 | tr -d ' ' | tr '\n' ' ' | sed 's/ $//')"

# --- grammars that must be rejected ------------------------------------------
printf 'aa = "A" ;\ncc = 3 * [aa], "C" ;\n' > "$tmp/cc.ebnf"          # ambiguous (standard 5.7)
printf 'aa = "A" ;\ngg = 3 * {aa}, "D" ;\n' > "$tmp/gg.ebnf"          # ambiguous (standard 5.7, note)
printf 'ident = { letter } - ( "A", "A" ) ;\nletter = "A" | "B" ;\n' > "$tmp/nonll1.ebnf"
printf 'S = ? anything ? ;\n' > "$tmp/special.ebnf"
printf '(* never closed\nS = "a" ;\n' > "$tmp/comment.ebnf"
printf 'S = "a" - T ;\nT = "b", T, "b" | "c" ;\n' > "$tmp/nonregular.ebnf"
printf 'S = "a" - S ;\n' > "$tmp/self.ebnf"
printf 'S = 3 "a" ;\n' > "$tmp/intnostar.ebnf"
printf 'S = "a", ;\n' > "$tmp/trailingcomma.ebnf"
for f in cc gg nonll1 special comment nonregular self intnostar trailingcomma; do rejects "$f" "$tmp/$f.ebnf"; done
has "cc is ambiguous" "$("$CFG" check "$tmp/cc.ebnf" 2>&1)" "both predicted by A"
has "nonll1 hint" "$("$CFG" check "$tmp/nonll1.ebnf" 2>&1)" "comes from an exception"
has "special sequence message" "$("$CFG" check "$tmp/special.ebnf" 2>&1)" "special sequences"
has "comment message" "$("$CFG" check "$tmp/comment.ebnf" 2>&1)" "unterminated comment"
has "nonregular message" "$("$CFG" check "$tmp/nonregular.ebnf" 2>&1)" "is not regular"
has "self message" "$("$CFG" check "$tmp/self.ebnf" 2>&1)" "depends on itself"

echo "iso: $pass passed, $fail failed"
[ "$fail" -eq 0 ]

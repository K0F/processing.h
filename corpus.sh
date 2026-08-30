#!/bin/sh
# corpus — batch-test the transpiler against a tree of wild sketches.
#
#   ./corpus.sh ../2010
#
# Every directory containing .pde tabs (applet/ copies excluded) is combined
# exactly like `run` does, transpiled and compiled. Results are classified:
#   PASS          transpiled, compiled and linked
#   TRANSPILE_ERR pde2c validation failed (file:line detail)
#   COMPILE_ERR   gcc failed (first diagnostic, #line-mapped to .pde source)
#
#   PDE2C=/path/to/pde2c         A/B test an alternative transpiler binary
#   PDE_CORPUS_TRANSPILE_ONLY=1  skip the gcc stage
#   PDE_CORPUS_KEEP=1            keep build dirs under /tmp for inspection
#
# Prints per-sketch progress to stderr; summary + failure histogram to
# stdout; full TSV table to corpus-results.txt.
set -u

SCRIPT_DIR=$(dirname "$(readlink -f "$0")")
ROOT="${1:-}"
[ -d "$ROOT" ] || { echo "corpus: usage: $0 <corpus-root>" >&2; exit 1; }

PDE2C_BIN="${PDE2C:-$SCRIPT_DIR/pde2c}"
if [ "$PDE2C_BIN" = "$SCRIPT_DIR/pde2c" ]; then
  if [ "$SCRIPT_DIR/pde2c.c" -nt "$SCRIPT_DIR/pde2c" ] || [ ! -x "$SCRIPT_DIR/pde2c" ]; then
    gcc "$SCRIPT_DIR/pde2c.c" -o "$SCRIPT_DIR/pde2c" || exit 1
  fi
fi

WORK=$(mktemp -d /tmp/pdecorpus.XXXXXX)
cleanup() { [ "${PDE_CORPUS_KEEP:-0}" = "1" ] || rm -rf "$WORK"; }
trap cleanup EXIT INT TERM

RESULTS="$PWD/corpus-results.txt"
: > "$RESULTS"

# one sketch dir per line: any dir holding a non-applet .pde tab
find "$ROOT" -name '*.pde' -not -path '*/applet/*' | sed 's|/[^/]*$||' | sort -u > "$WORK/dirs.txt"
TOTAL=$(wc -l < "$WORK/dirs.txt")

IDX=0
while IFS= read -r DIR; do
  IDX=$((IDX + 1))
  NAME=$(basename "$DIR")
  REL=${DIR#$ROOT/}
  BLD="$WORK/$IDX-$NAME"
  mkdir -p "$BLD"

  # combine tabs like run: main tab (dir-named) first, rest alphabetical
  MAIN="$BLD/none.pde"
  [ -f "$DIR/$NAME.pde" ] && MAIN="$DIR/$NAME.pde"
  {
    [ "$MAIN" != "$BLD/none.pde" ] && { printf '//@file %s\n' "$(basename "$MAIN")"; cat "$MAIN"; }
    for f in $(find "$DIR" -maxdepth 1 -name '*.pde' | sort); do
      [ "$f" = "$MAIN" ] && continue
      printf '//@file %s\n' "$(basename "$f")"
      cat "$f"
    done
  } > "$BLD/combined.pde"

  STATUS=""
  DETAIL=""
  if ! "$PDE2C_BIN" "$BLD/combined.pde" > "$BLD/sketch.c" 2> "$BLD/pde2c.err"; then
    STATUS="TRANSPILE_ERR"
    DETAIL=$(head -1 "$BLD/pde2c.err")
    [ -z "$DETAIL" ] && DETAIL="(no message)"
  elif [ "${PDE_CORPUS_TRANSPILE_ONLY:-0}" != "1" ]; then
    CFLAGS="${PDE_OPTS--O0 -pipe}"
    if gcc $CFLAGS $("$SCRIPT_DIR/pdedeps" --includes) "$BLD/sketch.c" \
         -o "$BLD/sketch" $("$SCRIPT_DIR/pdedeps" --libs) \
         2> "$BLD/gcc.err"; then
      STATUS="PASS"
    else
      STATUS="COMPILE_ERR"
      DETAIL=$(grep -m1 'error:' "$BLD/gcc.err" | sed 's|^[^:]*:[0-9]*:[0-9]*: *||')
      [ -z "$DETAIL" ] && DETAIL=$(head -1 "$BLD/gcc.err")
    fi
  else
    STATUS="TRANSPILE_OK"
  fi

  printf '%s\t%s\t%s\n' "$STATUS" "$REL" "$DETAIL" >> "$RESULTS"
  printf '[%d/%d] %-13s %s%s\n' "$IDX" "$TOTAL" "$STATUS" "$REL" \
    "${DETAIL:+ — $DETAIL}" >&2
done < "$WORK/dirs.txt"

# ---- summary -------------------------------------------------------------
printf '\n== summary (%d sketches, %s) ==\n' "$TOTAL" "$(basename "$ROOT")"
awk -F'\t' '{ n[$1]++ } END { for (s in n) printf "%-13s %d\n", s, n[s] }' "$RESULTS" | sort

if grep -q '_ERR' "$RESULTS"; then
  printf '\n== failure histogram ==\n'
  grep '_ERR' "$RESULTS" | cut -f3 | sed 's|'"$ROOT"'/||g' \
    | sort | uniq -c | sort -rn | head -15
fi
echo "results: $RESULTS"

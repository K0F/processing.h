#!/bin/bash
# runAndroid — build + run a sketch on-device (Android/Termux).
#
#   ./runAndroid.sh            sketch from the current directory
#   ./runAndroid.sh ~/sketch   sketch elsewhere
#
# Requires Termux + termux-x11 + openbox (see android-setup.sh). It sets up the
# X display, transpiles the sketch with pde2c, compiles it against the
# on-device raylib/freetype (via pdedeps), then runs it under the openbox X
# session provided by termux-x11.
set -u

SCRIPT_DIR=$(dirname "$(readlink -f "$0")")
DIR="${1:-$PWD}"

if [ ! -d "$DIR" ]; then
  echo "runAndroid: no such directory: $DIR" >&2
  exit 1
fi

# --- X display -----------------------------------------------------------
export XKB_CONFIG_ROOT=${XKB_CONFIG_ROOT:-$PREFIX/share/X11/xkb}
[ -e "$XKB_CONFIG_ROOT" ] || ln -s $PREFIX/share/xkeyboard-config-2 "$XKB_CONFIG_ROOT" 2>/dev/null

pkill -f termux-x11 2>/dev/null
pkill -f openbox 2>/dev/null
termux-x11 :1 &
sleep 1.5

export DISPLAY=:1

openbox &
sleep 0.5

# --- build ------------------------------------------------------------------
# Rebuild pde2c only when its source changed.
if [ "$SCRIPT_DIR/pde2c.c" -nt "$SCRIPT_DIR/pde2c" ] || [ ! -x "$SCRIPT_DIR/pde2c" ]; then
  gcc "$SCRIPT_DIR/pde2c.c" -o "$SCRIPT_DIR/pde2c" || exit 1
fi

TMP=$(mktemp -d /tmp/pdeandroid.XXXXXX)
trap 'rm -rf "$TMP"' EXIT INT TERM

MAIN="$TMP/none.pde"
[ -f "$DIR/$(basename "$DIR").pde" ] && MAIN="$DIR/$(basename "$DIR").pde"
{
  [ "$MAIN" != "$TMP/none.pde" ] && { printf '//@file %s\n' "$(basename "$MAIN")"; cat "$MAIN"; }
  for f in $(find "$DIR" -maxdepth 1 -name '*.pde' | sort); do
    [ "$f" = "$MAIN" ] && continue
    printf '//@file %s\n' "$(basename "$f")"
    cat "$f"
  done
} > "$TMP/combined.pde"

if ! grep -q '[^[:space:]]' "$TMP/combined.pde"; then
  echo "runAndroid: no .pde files found in $DIR" >&2
  exit 1
fi

if ! "$SCRIPT_DIR/pde2c" "$TMP/combined.pde" > "$TMP/sketch.c"; then
  echo "runAndroid: transpile failed" >&2
  exit 1
fi

CFLAGS="${PDE_OPTS--O0 -pipe}"
if ! gcc $CFLAGS $("$SCRIPT_DIR/pdedeps" --includes) "$TMP/sketch.c" -o "$TMP/sketch" \
     $("$SCRIPT_DIR/pdedeps" --libs) 2> "$TMP/gcc.err"; then
  head -15 "$TMP/gcc.err"
  echo "runAndroid: compilation failed" >&2
  exit 1
fi

"$TMP/sketch"

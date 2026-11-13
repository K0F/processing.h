#!/bin/sh
# pdecommon.sh — shell helpers shared by run, pdecc, wildtest and corpus.sh.
# Not executable on its own: callers source it after setting SCRIPT_DIR.
#
#   . "$SCRIPT_DIR/pdecommon.sh"
#   pde_build_transpiler "$SCRIPT_DIR" || exit 1
#   pde_pch_update "$SCRIPT_DIR" || exit 1
#   pde_merge_tabs "$SKETCH_DIR" "$TMP/combined.pde"

# Rebuild pde2c when its source is newer than the binary.
pde_build_transpiler() {
  _root=$1
  if [ "$_root/pde2c.c" -nt "$_root/pde2c" ] || [ ! -x "$_root/pde2c" ]; then
    gcc "$_root/pde2c.c" -o "$_root/pde2c"
  fi
}

# Is the precompiled header out of date?
#
# processing.h pulls in libraries/{minim,video,xml,geomerative,io}.h, and gcc
# does NOT revalidate headers nested inside a PCH: editing a library and
# keeping a stale .gch silently compiles the old API. So every file the PCH
# transitively depends on has to be part of the freshness test.
pde_pch_stale() {
  _root=$1
  _gch="$_root/processing.h.gch"
  [ -f "$_gch" ] || return 0
  _newer=$(
    { find "$_root" -maxdepth 1 -name 'processing.h' -newer "$_gch"
      find "$_root/libraries" -maxdepth 1 -name '*.h' -newer "$_gch"
    } 2>/dev/null | head -n 1
  )
  [ -n "$_newer" ]
}

# Rebuild the PCH if stale. Returns non-zero if the build failed.
#
# PDE_NOPCH=1 disables the PCH entirely. gcc uses a .gch whenever it sits next
# to the header, so "disable" means removing it — merely skipping the rebuild
# would keep compiling against the old one.
pde_pch_update() {
  _root=$1
  if [ "${PDE_NOPCH:-0}" = "1" ]; then
    rm -f "$_root/processing.h.gch"
    return 0
  fi
  pde_pch_stale "$_root" || return 0
  if ! gcc $("$_root/pdedeps" --includes) -x c-header "$_root/processing.h" \
       -o "$_root/processing.h.gch"; then
    rm -f "$_root/processing.h.gch"   # never leave a half-written PCH behind
    return 1
  fi
}

# Merge a sketch's .pde tabs into one file, exactly like the Processing IDE
# concatenates them: the tab named after the sketch directory goes first, the
# rest follow alphabetically, each prefixed with a `//@file <name>` marker so
# #line-mapped diagnostics point back at the right tab.
#
# NUL-safe: sketch directories with spaces in their names are common in
# archived corpora ("03-brezen/my sketch") and used to split into fake
# sketches when this was `for f in $(find ...)`.
pde_merge_tabs() {
  _dir=$1
  _out=$2
  _main="$_dir/$(basename "$_dir").pde"
  [ -f "$_main" ] || _main=

  : > "$_out"
  if [ -n "$_main" ]; then
    printf '//@file %s\n' "$(basename "$_main")" >> "$_out"
    cat "$_main" >> "$_out"
  fi
  find "$_dir" -maxdepth 1 -name '*.pde' -print0 | LC_ALL=C sort -z |
  while IFS= read -r -d '' _tab; do
    [ "$_tab" = "$_main" ] && continue
    printf '//@file %s\n' "$(basename "$_tab")" >> "$_out"
    cat "$_tab" >> "$_out"
  done
}

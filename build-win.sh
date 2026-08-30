#!/bin/sh
# build-win.sh — cross-compile pdeide + pde2c for Windows (MinGW x86_64).
#
# Produces, in the repo build/win64-deps tree:
#   pdeide.exe  pde2c.exe
# plus the vendored static raylib + freetype archives built for Windows
# under build/win64-deps/static/... (kept separate from the Linux
# build/static used by pdedeps/export, so neither build disturbs the other).
#
# Requires: x86_64-w64-mingw32-gcc (package mingw-w64).
set -eu

root=$(cd "$(dirname "$0")" && pwd)
bdir="$root/build/win64-deps"
depdir="$bdir/static"
toolchain="$root/cmake/toolchains/mingw-w64-x86_64.cmake"
CC=x86_64-w64-mingw32-gcc

echo "== building Windows static raylib + freetype =="
cmake -S "$root/third_party/raylib" -B "$depdir/raylib" \
  -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
  -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_EXAMPLES=OFF -DBUILD_GLFW=OFF -DGLFW_BUILD_WAYLAND=OFF
cmake --build "$depdir/raylib"

cmake -S "$root/third_party/freetype" -B "$depdir/freetype" \
  -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
  -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release \
  -DFT_DISABLE_HARFBUZZ=TRUE -DFT_DISABLE_BROTLI=TRUE \
  -DFT_DISABLE_BZIP2=TRUE -DFT_DISABLE_PNG=TRUE -DFT_DISABLE_ZLIB=TRUE
cmake --build "$depdir/freetype"

RAYLIB_STATIC="$depdir/raylib/raylib/libraylib.a"
FREETYPE_STATIC="$depdir/freetype/libfreetype.a"
RAYLIB_SRC="$root/third_party/raylib/src"
FREETYPE_SRC="$root/third_party/freetype/include"
FREETYPE_BUILD="$depdir/freetype/include"

for a in "$RAYLIB_STATIC" "$FREETYPE_STATIC"; do
  [ -f "$a" ] || { echo "missing: $a" >&2; exit 1; }
done

echo "== building pde2c.exe =="
$CC "$root/pde2c.c" -o "$bdir/pde2c.exe" -O2 -I"$root"

echo "== building pdeide.exe =="
$CC "$root/pdeide.c" "$root/platform.c" "$root/editor.c" "$root/tinyfiledialogs.c" \
  -o "$bdir/pdeide.exe" -O2 \
  -I"$root" -I"$RAYLIB_SRC" -I"$FREETYPE_SRC" -I"$FREETYPE_BUILD" \
  "$RAYLIB_STATIC" "$FREETYPE_STATIC" \
  -lopengl32 -lgdi32 -lwinmm -lshell32 -luser32 -lole32 -lcomdlg32 -static

echo "== done: =="
ls -la "$bdir"/pdeide.exe "$bdir"/pde2c.exe

echo "== packaging Windows release zip =="
rdir="$bdir/../dist/pdeide-win64"
rm -rf "$rdir"
mkdir -p "$rdir/build/static/raylib/raylib" \
         "$rdir/build/static/freetype/include" \
         "$rdir/third_party/raylib/src" \
         "$rdir/third_party/freetype/include"

cp "$bdir/pdeide.exe" "$bdir/pde2c.exe" "$rdir/"
cp "$root/processing.h" "$root/editor.h" "$root/platform.h" \
   "$root/sketch.pde" "$root/terminus.ttf" "$root/terminus_ttf.h" "$rdir/"
cp "$RAYLIB_STATIC" "$rdir/build/static/raylib/raylib/libraylib.a"
cp "$FREETYPE_STATIC" "$rdir/build/static/freetype/libfreetype.a"
cp -r "$FREETYPE_BUILD"/. "$rdir/build/static/freetype/include/"
cp -r "$FREETYPE_SRC"/. "$rdir/third_party/freetype/include/"
cp -r "$RAYLIB_SRC"/. "$rdir/third_party/raylib/src/"

(cd "$rdir/.." && rm -f pdeide-win64.zip)
if command -v zip >/dev/null 2>&1; then
  (cd "$rdir/.." && zip -qr pdeide-win64.zip pdeide-win64)
elif command -v 7z >/dev/null 2>&1; then
  (cd "$rdir/.." && 7z a -tzip pdeide-win64.zip pdeide-win64 >/dev/null)
else
  python3 - "$rdir" <<'PY'
import sys, zipfile, os
root = sys.argv[1]
out = os.path.join(os.path.dirname(root), "pdeide-win64.zip")
if os.path.exists(out): os.remove(out)
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for dp, _, fns in os.walk(root):
        for fn in fns:
            p = os.path.join(dp, fn)
            z.write(p, os.path.relpath(p, os.path.dirname(root)))
PY
fi
echo "windows zip: $bdir/../dist/pdeide-win64.zip"


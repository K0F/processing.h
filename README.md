# Processing in Raylib

Yet another attempt to free Processing from its Java runtime. `pde2c`
transpiles `.pde` sketches to plain C, and `processing.h` — a single-header
runtime — implements the Processing API on top of raylib. A small ray-based
IDE (`pdeide`) wraps the whole chain for editing, running and exporting.

![pdeide — Processing sketch editor](docs/screenshot.png)

> Screenshot coming soon — the file lives at `docs/screenshot.png`.
> This is ALPHA quality: useful, honest, incomplete.

## What works

- **Transpiler**: `pde2c` rewrites `.pde` → C (types, `new`, `ArrayList`,
  user classes, PVector, Java-ism casts, string concat, array helpers).
- **Runtime**: rendering, color, math/noise/random, input, time, text,
  transforms, PVector, PImage/loadPixels, 3D primitives (P3D sketches).
- **IDE**: editing, syntax highlighting, Run/Stop, console with diagnostics
  mapped back to your `.pde` lines, native open/save dialogs, and an Export
  button that produces a statically linked, self-contained binary.

Job lists live in [TODO.md](TODO.md) and the git history. Not supported yet:
inheritance, generics, external Java libraries (imports are stripped),
`try/catch`, most `String` methods.

## Quick start (run a sketch)

```sh
./run                    # sketch in current directory
./run ~/path/to/sketch   # sketch elsewhere
./pdecc ~/my/sketch.pde  # build an optimized binary next to the source
./pdecc -n sketch.pde    # build only, don't run
```

`run` transpiles + compiles + runs in a temp dir; `pdecc` writes a persistent
binary and reuses a precompiled header for ~0.7s turnaround.

## Building the IDE with CMake

Vendored raylib + FreeType are git submodules. Configure on a fresh clone
bootstrap them automatically:

```sh
cmake -S . -B build -DPDEIDE_STATIC_DEPS=ON
cmake --build build -j
ctest --test-dir build          # editor unit tests
./build/pdeide [sketch.pde]     # launch the IDE
```

To use system raylib/FreeType instead (requires `libraylib-dev` +
`libfreetype-dev`), configure with `-DPDEIDE_STATIC_DEPS=OFF`.

Build a Linux release tarball with:

```sh
cmake --build build --target release    # -> build/pdeide-linux.tar.gz
```

The tarball preserves the repo layout (pde2c, static archives, fonts) so the
IDE's Run/Export work from any unpack location.

## Testing against real-world corpora

```sh
./wildtest ~/src/2021    # transpile + gcc syntax-check, PASS/FAIL tally
./corpus.sh ../2010      # transpile + full link, classified results
```

## Dependencies

Fresh Debian: `libx11-dev libgl-dev libasound2-dev libfreetype-dev` plus the
steps in [setup-deps.sh](setup-deps.sh).

Author: Kof, 2026 — community software, provided as-is.
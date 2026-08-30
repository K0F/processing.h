# Processing in Raylib

Transpile `.pde` Processing sketches to plain C and run them on raylib,
without Java. `pde2c` does the transpiling, `processing.h` is the
single-header runtime API, and `pdeide` is a small editor wrapping the chain.

![pdeide — Processing sketch editor](screenshot.png)

> ALPHA quality: useful, honest, incomplete. Not supported yet: inheritance,
> generics, external Java libraries, `try/catch`, most `String` methods.
> Real today: shapes, transforms, text, images, the G1-G11 reference-audit
> stubs, plus true `min()/max()` (3-arg + arrays), `delay()`/`cursor()`/
> `noCursor()`, `binary()`/`unbinary()`, `trim()`, `byte()` (clamp)/
> `boolean()`, and `pushStyle()`/`popStyle()`.

## Quick start

```sh
./run                    # run the sketch in the current directory
./pdecc ~/my/sketch.pde  # build an optimized binary next to the source
./build/pdeide           # launch the editor
```

## Build

```sh
cmake -S . -B build            # submodules (raylib, freetype) bootstrapped automatically
cmake --build build -j
ctest --test-dir build         # headless unit tests (no window/GPU needed)
cmake --build build --target release   # -> build/pdeide-linux.tar.gz
```

## Tests & health checks

Unit tests cover the pde2c editor plus the pure `processing.h` functions
(`make test` = 45 editor checks + 87 runtime checks; verified via `ctest`).

Real-world sketches are batch-checked against the transpiler:

```sh
./wildtest ~/src/2010                # transpile + gcc -fsyntax-only, per-sketch pass/fail
./wildtest ~/src/LearningProcessing  # book corpus (2015, processing-3 era)
./corpus.sh ../2010                  # transpile + full link, summary to corpus-results.txt
```

Current corpus status:

| corpus | pass |
| ----- | ---- |
| 2010 sketch archive (`~/src/2010`) | 21/168 |
| shiffman/LearningProcessing | 122/428 |

Failures are missing third-party libraries (`OscP5`, `Minim`, `PeasyCam`,
`PGraphicsOpenGL`, `Capture`/`Serial`, …) and other unimplemented API — both
corpora verified byte-for-byte identical against the pre-feature baseline
(no regressions). See [TODO.md](TODO.md) for the roadmap.

Author: Kof, 2026 — community software, provided as-is.
# Processing in Raylib

Transpile `.pde` Processing sketches to plain C and run them on raylib,
without Java. `pde2c` does the transpiling, `processing.h` is the
single-header runtime API, and `pdeide` is a small editor wrapping the chain.

![pdeide — Processing sketch editor](screenshot.png)

> ALPHA quality: useful, honest, incomplete.

**Currently supported features:**
-   **Core drawing**: shapes, transforms, text, images
-   **Key APIs**: G1-G11 reference-audit stubs, `min()/max()` (3-arg + arrays), `delay()/cursor()/noCursor()`
-   **Data conversion/manipulation**: `binary()/unbinary()`, `trim()`, `byte()` (clamped 0-255), `boolean()`
-   **Style management**: `pushStyle()/popStyle()`

**Limitations:**
-   No inheritance, generics, or external Java libraries.
-   `try/catch` not implemented.
-   Most `String` methods are not yet supported.

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

**Unit tests** verify the `pde2c` transpiler and `processing.h` runtime functions.
-   `make test` runs 45 editor checks and 87 runtime checks, verified by `ctest`.

**Real-world corpus tests** run a batch of Processing sketches through the transpiler:

```sh
./wildtest ~/src/2010                # transpile + gcc -fsyntax-only, per-sketch pass/fail
./wildtest ~/src/LearningProcessing  # book corpus (2015, Processing 3 era)
./corpus.sh ../2010                  # transpile + full link, summary to corpus-results.txt
```

**Current corpus status:**

| Corpus | Pass Rate |
| :------------------------------- | :--------: |
| 2010 sketch archive (`~/src/2010`) | 21/168     |
| shiffman/LearningProcessing      | 122/428    |

Failures typically stem from missing third-party libraries (`OscP5`, `Minim`, `PeasyCam`, `PGraphicsOpenGL`, `Capture`/`Serial`, etc.) or other unimplemented Processing API. Both corpora have shown **no regressions** when compared byte-for-byte against the pre-feature baseline. See [TODO.md](TODO.md) for the roadmap.


Author: Kof, 2026 — community software, provided as-is.
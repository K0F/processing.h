# Project Manifest & Roadmap: /processing

An immediate-mode creative coding sandbox for pure C, powered by Raylib.
Sketches in Processing-style `.pde` are transpiled to C (`pde2c`) and run on
the single-header `processing.h` runtime; a bundled editor (`pdeide`) and a
CMake build with vendored raylib + FreeType make it self-contained.

---

## Phase 1: Core Engine (Completed Milestone)
- [x] **Ambient State Machine:** Abstracted `fill()`, `stroke()`, and `strokeWeight()` logic.
- [x] **Dynamic Window Scope:** Global `width`, `height`, `mouseX`, `mouseY`, and `mousePressed` tracked per-frame.
- [x] **Typography Engine:** Texture-filtered, coordinate-snapped `text()` rendering.
- [x] **Low-Level Access:** Framebuffer wrapping via `PGraphics` and direct CPU pixel array manipulation (`loadPixels()` / `updatePixels()`).
- [x] **I/O Capture:** Fast `save()` snapshots and frame-indexed sequential render outputs (`saveFrame("###.png")`).

---

## Phase 2: Transpiler Pipeline (`pde2c`)
- [x] **Type Normalization Layer:**
  - Maps Java primitives (`boolean` -> `bool`, `color` -> `uint32_t`, `String` -> `const char *`, `byte` -> `uint8_t`).
  - Embeds custom `color` parsing macros to intercept implicit RGBA constructors like `color(r, g, b)`.
- [x] **AST / Prototype Generator:**
  - Tokenizes source, scans user-defined functions, and automatically emits forward declarations at the top of the generated `.c` file to satisfy C's strict structural layout.
- [x] **Automated Main Injection:**
  - Auto-wraps `setup()` and `draw()` loops within the Raylib window thread lifecycles.
- [x] **Native Math Overloads:**
  - Aliases common Processing operations directly to their standard C equivalents (`random()`, `noise()`, `sin()`, `atan2()`).

---

## Phase 3: Object & Vector Translation
- [x] **PVector Infrastructure:**
  - Stack-allocated C alternative:
    ```c
    typedef struct { float x, y, z; } PVector;
    ```
  - Functional math alternatives since C lacks object methods:
    ```c
    PVector pvector_add(PVector v1, PVector v2);
    PVector pvector_sub(PVector v1, PVector v2);
    ```
- [x] **Object Structure Flattening:**
  - Lightweight Java classes map to flat C structs (see Phase 6).

---

## Phase 4: Image Handling (PImage)
- [x] **Load / create / draw:** `loadImage` (normalized to RGBA8), `createImage`, `image()` (type-dispatched between canvases and images).
- [x] **Tint & pixels:** `tint/noTint`, GPU texture caching, direct `img.pixels[i]` access.
- [x] **Member ops rewritten by transpiler:** `loadPixels/updatePixels/filter/mask/resize/save`, `get()` (packed color, copy, region), `beginDraw/endDraw` on PGraphics.

---

## Phase 5: 3D, Transforms, Strings & Arrays
- [x] **3D (OPENGL sketches):** `translate(x,y,z)`, renderer constants (`P2D/P3D/OPENGL`), `sphere()/sphereDetail()`, `box()`, `camera()`, `normal()`, `PMatrix/getMatrix()/applyMatrix()`.
- [x] **Transforms:** `pushMatrix/popMatrix/translate/rotate/rotateX/Y/Z/shearX/shearY/resetMatrix`.
- [x] **Strings:** concatenation (`"a: " + x` -> `_pde_cat`), `.charAt(i)`, `split()/splitTokens()/join()`, `nf/nfs/nfp/nfc`, `println()` bare values.
- [x] **Arrays:** heap arrays `new TYPE[n]` with length registry, `.length` dispatch, `append/expand/concat/subset/shorten/reverse/splice/sort`.

---

## Phase 6: User-defined Classes + ArrayList
- [x] **Class translation:** `class Name { ... }` -> `typedef struct`, constructors emit `Name Name_ctor(args)`, methods emit `Name_meth(Name *self, ...)`.
- [x] **Member rewriting:** `this.`/bare-field refs -> `self->`, field initializers at ctor start, `new Name(...)` -> `Name_ctor(...)`, sketch-level `obj.method(...)` routed to the same functions.
- [x] **ArrayList runtime:** `add/set/get/size/clear/remove`, `new ArrayList()`, type-erased copy-on-add, including the `(Type)list.get(i)` cast-get idiom.

---

## Phase 7: Interactive IDE (`pdeide`)
- [x] **Editor engine (`editor.c`):** click-exact caret/selection on a pixel grid, line moves, pretty-format, error markers; unit-tested standalone.
- [x] **Syntax highlighting:** olive-dark theme, keywords/strings/numbers/comments, multi-line block comments.
- [x] **Run/Stop:** builds the current buffer through `pde2c` -> gcc (vendored deps first, system fallback) and streams the child's stdout/stderr live, with `file:line` diagnostics mapped back to editor lines.
- [x] **Pixel-crisp fonts:** authentic embedded bitmap strikes from `terminus.ttf` via FreeType (shared renderer in `processing.h` too).
- [x] **Native dialogs:** open/save/export via bundled `tinyfiledialogs`, with a text-input fallback.
- [x] **Static export:** one-click self-contained binary (statically linked raylib+freetype, embedded font).

---

## Phase 8: Build & Packaging
- [x] **CMake build:** `cmake -S . -B build` builds the IDE, transpiler and tests;
  vendored raylib + FreeType submodules are bootstrapped automatically.
- [x] **Platform layer:** `platform.[ch]` abstracts process spawn/kill/pipe, exec-dir
  and temp-dir handling so `pdeide.c` stays portable (POSIX implemented; best-effort `_WIN32`).
- [x] **Self-contained deps:** `pdedeps` emits compile/link flags preferring the
  CMake-built static archives (`build/static`), falling back to system packages;
  `make`, `./run`, `./pdecc`, `wildtest`, `corpus.sh` and the IDE all share it.
- [x] **Linux release tarball:** `cmake --build build --target release` produces
  `build/pdeide-linux.tar.gz` mirroring the repo layout (tag `v0.1.0-alpha`).
- [x] **Robustness harness:** `processing.h.gch` precompiled header, `wildtest` /
  `corpus.sh` PASS/FAIL tallies against real-world sketch trees.

---

## One-Shot Compiler CLI (`pdecc`)
- [x] **One-shot compiler CLI:** unified `./pdecc` script transpiles + compiles a
  `.pde` sketch (single file, directory, or cwd) into an optimized native
  binary next to the source, using the precompiled header for ~0.7s turnaround.
  Flags: `-o <bin>`, `-O<N>`, `-n/--no-run`, `--keep`.

## Next Steps (Unfinished)
- [ ] **Inheritance & generics / type parameters** (`extends`, `<T>`).
- [ ] **Exception handling:** `try/catch`.
- [ ] **More `String` methods:** `indexOf`, `substring`, `toLowerCase`, `toUpperCase`, `replace`, ...
- [ ] **Path building:** `curveVertex` / `bezierVertex` inside arbitrary (non-`beginShape(CLOSE)`) paths.
- [ ] **File IO:** `loadStrings` / `saveStrings` and text-file reading/writing.
- [ ] **PGraphics receiver drawing:** `pg.stroke()`-style drawing calls on a `PGraphics` receiver (only `beginDraw/endDraw` are routed today).
- [ ] **Windows validation:** compile + run the IDE on `_WIN32` (platform layer is written but untested).
- [ ] **CI:** GitHub Actions matrix (Linux/macOS/Windows) building, testing and producing the release tarball.
- [ ] **Multi-platform export from the IDE:** macOS/Windows static binaries alongside the Linux one.
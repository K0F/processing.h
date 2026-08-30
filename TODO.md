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
- [x] **Builtin `Object` value class** (empty struct + implicit ctor) so `new Object()` / `(Object) list.get(i)` compile.
- [x] **No-background semantics:** canvas is wiped only once at startup; explicit `background()` calls clear it during the frame it is invoked, so sketches that never call `background()` keep prior frames (Processing-style trails) instead of a per-frame default-gray clear.
- [x] **Generics-safe ArrayList:** `ArrayList<Type>` accepted in globals, function locals, params and class fields (type args dropped, still routed as `ArrayList`); member ops routed inside `setup()`/`draw()`/helpers too.

---

## Phase 7: Interactive IDE (`pdeide`)
- [x] **Editor engine (`editor.c`):** click-exact caret/selection on a pixel grid, line moves, pretty-format, error markers; unit-tested standalone.
- [x] **Syntax highlighting:** olive-dark theme, keywords/strings/numbers/comments, multi-line block comments.
- [x] **Run/Stop:** builds the current buffer through `pde2c` -> gcc (vendored deps first, system fallback) and streams the child's stdout/stderr live, with `file:line` diagnostics mapped back to editor lines.
- [x] **Pixel-crisp fonts:** authentic embedded bitmap strikes from `terminus.ttf` via FreeType (shared renderer in `processing.h` too).
- [x] **Native dialogs:** open/save/export via bundled `tinyfiledialogs`, with a text-input fallback.
- [x] **Static export:** one-click self-contained binary (statically linked raylib+freetype, embedded font).
- [x] **Default window:** editor opens at 814x576 (layout derives from `GetScreenWidth/Height`, so it also resizes freely).

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

## Phase 9: API Completeness (Processing reference audit)
Audit of `processing.h` against https://processing.org/reference. Where a documented
command was missing entirely, a compile-compatible stub (no-op body, marked `TODO`)
keeps sketches building; verified-present commands are ticked. Deferred work sits in
the "Backlog" block below.
- [x] **G1 curves & contours:** `curveVertex`, `quadraticVertex`, `beginContour`, `endContour`.
- [x] **G2 transform / projection / fullscreen:** `printMatrix`, `perspective` (0/4-arg), `frustum`, `beginCamera`, `endCamera`, `fullScreen`.
- [x] **G3 style stack:** `pushStyle`, `popStyle`.
- [x] **G4 color modes:** `colorMode` 1..5-arg forms routed (was capped at 4; `colorMode5` added + pde2c arity cap lifted).
- [x] **G5 typography:** `textMode`, `textWrap` stubs; `textAlign(1/2)` + `textFont`/`textSize`/`loadFont`/`textAscent`/`textDescent`/`textLeading` already implemented.
- [x] **G6 image:** `imageMode`, `blend` (image op), `PShape` + `loadShape`, `createShape` stubs.
- [x] **G7 lights & materials:** `ambientLight`, `directionalLight`, `pointLight`, `spotLight`, `lightSpecular`, `lightFalloff`, `ambient`, `emissive`, `specular`, `shininess`, `texture`, `textureWrap`.
- [x] **G8 environment:** `delay`, `cursor` (+ cursor constants `ARROW/CROSS/HAND/MOVE/TEXT_CURSOR/WAIT`), `noCursor`.
- [x] **G9 data conversion:** `byte`, `boolean` (`_Generic` wrappers), `binary` (1/2-arg), `unbinary`; `char(x)` is already valid C as a cast.
- [x] **G10 string utilities:** standalone `trim(String[])`, `match`, `matchAll` free functions.
- [x] **G11 PVector statics:** standalone `random2D`, `random3D` free functions.
- [x] **Constants for stub modes:** `WRAP`/`CHAR`/`WORD`, `MODEL`/`SHAPE`, `REPEAT`/`CLAMP`/`MIRROR`, cursor types, `colorMode` uses existing `RGB`/`HSB`.

### Backlog (deferred fixes from the audit)
- [ ] **Value-mismatch fixes:** `round`/`floor`/`ceil` return `int` (currently `float`); `hex()` adds a `#` prefix Processing does not emit and lacks the `digits` arity; `nf`/`nfc` need array forms and `nfc` must keep its 2 decimals.
- [ ] **Method-call forms of stubbed commands:** `PVector.random2D()`, `PVector.random3D()`, `String.trim()`, `str.trim()`-style calls need pde2c rewriter support before they can compile.
- [ ] **Promote stubs to real implementations** once a sketch actually needs each body.
- [ ] **Verify remaining constants:** `hue`/`saturation`/`brightness` output ranges and `beginShape` `OPEN`/`CLOSE` enum values vs real Processing ints (`CLOSE=145`).

## Phase 10: Real implementations of the Phase-9 stubs (difficulty [1..5] per step)
Strategy: do the pure libraries first (headless unit-testable), wire the
method-call forms, then add renderer state in small layers, then the
shape/path subsystem. Each line below lands as its own commit.

### Tier A - Pure functions (unit tests via ctest, no GPU)
- [x] delay(), cursor()/noCursor() real (CPU-delay; raylib Show/HideMouse + SetMouseCursor)  [1]
- [x] min()/max() 3-arg + int[]/float[] array forms  [1]
- [ ] nf()/nfc() array forms; nfc() keeps 2 decimals  [2]
- [ ] byte()/boolean() real clamp/bool conversions; char() kept as C cast  [1]
- [x] binary()/unbinary() real bit-string conversion  [1]
- [ ] hex() real (drop '#' prefix) + digits arity  [2]
- [ ] PMatrix 2D API + printMatrix  [2]
- [x] trim(String[]) real (strip + realloc arrays)  [1]
- [ ] String instance methods refactored to _pde_str_* (indexOf, substring, toLowerCase/UpperCase, replace, equals)  [3]
- [ ] PVector instance + static methods (set/mag/add/sub/mult/div/dist/dot/cross/normalize/limit/heading/rotate/lerp/angleBetween/array/fromAngle + random2D/random3D)  [3]
- [ ] match()/matchAll() - needs a minimal regex engine (vendored or ~300-line DFA)  [4]

### Tier B - pde2c method-call rewiring
- [ ] Route String receivers to _pde_str_*; expose PVector statics as accessors  [2]

### Tier C - Renderer state, small increment each
- [ ] pushStyle()/popStyle() (save/restore existing fill/stroke/tint id + widths)  [1]
- [ ] fullScreen() (ToggleFullscreen + rederived layout, 0/1-arg forms)  [2]
- [ ] colorMode() HSB real: store colors in HSB bucket, keep hue/sat/brightness outputs consistent  [2]
- [ ] imageMode() + image blend() (compose via draw-into-texture with raylib blend modes)  [3]
- [ ] texture()/textureWrap() into the existing 3D path (rlSetTextureWrap + upload)  [4]
- [ ] perspective()/frustum() + beginCamera()/endCamera() (custom projection matrices)  [4]
- [ ] lighting/material pipeline: ambient/directional/point/spot light + lightSpecular/lightFalloff + ambient/emissive/specular/shininess  [5]

### Tier D - Shape/path subsystem
- [ ] curveVertex() via Catmull-Rom in a dedicated curve buffer (ignores prior vertex(), matches Processing)  [2]
- [ ] quadraticVertex() chained conic segments; bezierVertex() gets C1-continuity hint  [2]
- [ ] beginContour()/endContour() - append sub-polygons into the ear-clip tessellator  [3]
- [ ] loadShape()/createShape()/PShape - parser + shape cache + draw (needs file IO)  [5]

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
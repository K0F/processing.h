# Project Manifest & Roadmap: /processing

A zero-dependency, immediate-mode creative coding sandbox for pure C, powered by the Raylib core.

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

## Next Steps (Unfinished)
- [ ] **One-Shot Compiler CLI (`pdecc`):**
  - Consolidate the `run` / `pp2c` wrapper scripts into a unified shell execution script.
  - Usage goal: `./pdecc sketch.pde` yields an optimized native binary in less than half a second.
- [ ] **Inheritance & generics / type parameters** (`extends`, `<T>`).
- [ ] **Exception handling:** `try/catch`.
- [ ] **More `String` methods:** `indexOf`, `substring`, `toLowerCase`, `toUpperCase`, `replace`, ...
- [ ] **Path building:** `curveVertex` / `bezierVertex` inside arbitrary (non-`beginShape(CLOSE)`) paths.
- [ ] **File IO:** `loadStrings` / `saveStrings` and text-file reading/writing.
- [ ] **PGraphics receiver drawing:** `pg.stroke()`-style drawing calls on a `PGraphics` receiver (only `beginDraw/endDraw` are routed today).

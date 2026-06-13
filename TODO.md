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
- [ ] **Type Normalization Layer:**
  - Map Java primitives (`boolean` -> `bool`, `int` arrays).
  - Embed custom `color` parsing macros to intercept implicit RGBA constructors like `color(r, g, b)`.
- [ ] **AST / Prototype Generator:**
  - Script a regex or token parser to scan user-defined functions.
  - Automatically emit forward declarations at the top of the generated `.c` file to satisfy C's strict structural layout.
- [ ] **Automated Main Injection:**
  - Auto-wrap `setup()` and `draw()` loops within the Raylib window thread lifecycles.
- [ ] **Native Math Overloads:**
  - Alias common Processing operations directly to their standard C equivalents (`random()`, `noise()`, `sin()`, `atan2()`).

---

## Phase 3: Object & Vector Translation
- [ ] **PVector Infrastructure:**
  - Implement a stack-allocated C alternative:
    ```c
    typedef struct { float x, y, z; } PVector;
    ```
  - Provide functional math alternatives since C lacks object methods:
    ```c
    PVector pvector_add(PVector v1, PVector v2);
    PVector pvector_sub(PVector v1, PVector v2);
    ```
- [ ] **Object Structure Flattening:**
  - Establish a methodology for handling lightweight Java classes by mapping them to flat C structures.

---

## Future Enhancements
- [ ] **One-Shot Compiler CLI (`pdecc`):**
  - Package the preprocessor script and the `gcc` engine flags into a unified shell execution script.
  - Usage goal: `./pdecc sketch.pde` yields an optimized, native binary in less than half a second.

# Processing in Raylib

Yet another try to free Processing language from Java behind it. This project is not complete but performs some pre-compilations steps needed to reproduce a simplicity of Processing language to pure C. It is using Raylib for rendering and other functionality.

... more instructions after alpha stage

## How it works

`pde2c` is a small transpiler that reads a `.pde` sketch, tokenizes it, applies a
set of rewrite rules (Java-isms to C), emits prototypes for helper functions and
prints a plain `.c` file that includes `processing.h` — a single-header runtime
implementing the Processing API on top of raylib.

```
sketch.pde -> pde2c -> sketch.c -> gcc (with -lraylib) -> native binary
```

## Dependencies

One script prepares a fresh Debian machine: X11/GL/ALSA development headers
via apt, then raylib itself cloned and built from source into `/usr/local`
(Debian ships no usable raylib package):

```sh
./setup-deps.sh
```

## Build & run a sketch

```sh
# build the transpiler
gcc pde2c.c -o pde2c

# transpile + compile + run your sketch
./pde2c mysketch.pde > mysketch.c
gcc mysketch.c -o mysketch -O2 -I. -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
./mysketch
```

Or use the `run` wrapper, which does all of it in a temp dir and cleans up
after the window closes:

```sh
./run                    # sketch in current directory (all .pde tabs together)
./run ~/path/to/sketch   # sketch elsewhere
PDE_OPTS=-O2 ./run       # override fast -O0 default compile
PDE_KEEP=1 ./run         # keep the tmp dir to inspect generated C
```

## Testing against real-world corpora

Two batch tools check every sketch directory under a tree (tabs merged
exactly like `run`, `applet/` copies skipped by `corpus.sh`):

```sh
./wildtest ~/src/2021        # transpile + gcc syntax-check, PASS/FAIL tally
./corpus.sh ../2010          # transpile + full gcc link, classified results
```

`wildtest` prints one line per sketch and a tally — quick pass-rate signal.
`corpus.sh` classifies `PASS / TRANSPILE_ERR / COMPILE_ERR`, prints a
failure-reason histogram, writes a TSV table to `corpus-results.txt` and
supports A/B testing an alternative transpiler via `PDE2C=/path/to/pde2c`;
`PDE_CORPUS_TRANSPILE_ONLY=1` skips the gcc stage.

Syntax errors (unbalanced brackets) are caught before compilation with
`file:line` messages pointing at your `.pde`; gcc errors on generated code are
mapped back to the original source lines via `#line` directives.

## What works

- **Rendering**: point, line, rect/ellipse/circle/square with `rectMode` /
  `ellipseMode`, triangle, quad, arc, bezier, polygon fill via ear clipping
  (`beginShape/vertex/endShape(CLOSE)` incl. concave shapes), strokes with
  `strokeWeight`, canvas-based `loadPixels/updatePixels`
- **Color**: `color()` packing, hex literals (`#FF8800`), `red/green/blue/alpha`,
  `brightness/saturation/hue`, `lerpColor`
- **Math & random**: `constrain/dist/mag/norm/sq/map`, Java-style `%` (works on
  floats), Perlin `noise` + `noiseDetail`, `randomSeed`, `randomGaussian`
- **Input**: `key/keyCode/keyPressed`, mouse position/buttons, plus
  `keyPressed()/keyReleased()/mousePressed()/mouseReleased()` callbacks
- **Time / loop**: `millis`, `second/minute/hour/day/month/year`,
  `frameRate()`, `noLoop/loop/redraw/exit`
- **Text**: fonts, sizes, `textAlign`, printing numbers directly via `text(3+4, x, y)`
- **Transforms**: `pushMatrix/popMatrix/translate/rotate/rotateX/Y/Z/shearX/shearY/resetMatrix`
- **PVector**: full static-method set including arrays (`new PVector[10]`) and
  method-call rewriting (`v.add(w)` etc.)
- **Sketch structure**: `settings()`, `setup()`, `draw()`, forward declarations
  generated automatically, heap arrays `new float[n]`, `boolean/color/String`
  type mapping, `final` dropped, Java `import`/`package` statements stripped
  (mid-file safe)
- **Java-isms**: Java-style arrays in both orders
  (`float[] a` and `float a[]`, incl. lists like `float a[], b;` and array
  parameters), `boolean[]` + `new boolean[n]`, trailing-array returns
  (`boolean sieve(int n)[]`), Java `%` stays int-preserving via `_Generic`,
  type-preserving `abs`, labeled breaks (`loop: for(...) { break loop; }`),
  variable/function name collisions auto-renamed (`NAME` -> `NAME_fn`)
- **Strings**: `"a: " + x + "!"` concatenation folded into `_pde_cat` calls
  (numbers formatted `%g`), `.charAt(i)` mapped to a helper returning a
  1-char string, `color()` arity-routed incl. single packed arg
- **PImage**: disk loading (`loadImage`, normalized to RGBA8), `createImage`,
  `image()` type-dispatched between canvases and images via `_Generic`,
  `tint/noTint`, GPU texture caching, direct `pixels[]` access
  (`img.pixels[i]` aliases the RGBA buffer), member ops rewritten by the
  transpiler: `loadPixels/updatePixels/filter/mask/resize/save`,
  `beginDraw/endDraw` on PGraphics
- **3D (OPENGL sketches)**: `translate(x,y,z)`, renderer constants
  (`P2D/P3D/OPENGL` accepted by `size()`), `sphere()/sphereDetail()` drawn
  under a temporary perspective projection (fill faces + stroke wires),
  `PMatrix/getMatrix()/applyMatrix()` backed by raylib matrices

Out of the ~20 real-world sketches in `~/src/2021` used as a test corpus, 9
currently compile, link and run natively (`./wildtest ~/src/2021`). On the
larger 2010 archive (151 sketches, `./corpus.sh ../2010`), 16 fully
transpile and link; the dominant blockers there are external-library types
(Minim, GL, OscP5, PeasyCam), user-defined classes (`ArrayList`, custom
types) and non-constant global initializers.

## Not supported yet

Classes and inheritance, `ArrayList` and collections, generics,
external libraries (`import` lines are stripped, but the library calls
themselves do not link: OscP5, video, MidiBus),
try/catch, most `String` methods beyond `charAt`,
`curveVertex/bezierVertex` inside arbitrary paths,
`loadStrings/loadImage` file IO, drawing calls on PGraphics receivers
(`pg.stroke()` style — only `beginDraw/endDraw` are routed).

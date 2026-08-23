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
  type mapping, `final` dropped

Out of ~100 real-world sketches used as a test corpus, 17 currently compile
and run natively. The rest are blocked by deeper Java features.

## Not supported yet

Classes and inheritance, `ArrayList` and collections, generics, string
concatenation (`"a" + x`), `import` and external libraries (OscP5, video),
try/catch, `String` methods, `curveVertex/bezierVertex` inside arbitrary paths,
`loadStrings/loadImage` file IO.

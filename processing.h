#ifndef PROCESSING_H
#define PROCESSING_H

#define _VERSION_ "0.1"

#include <stdio.h>
#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#ifndef R_OK
#define R_OK 4
#endif
#define access _access
#define PROCESSING_WIN32 1
#else
#include <sys/time.h>
#include <unistd.h>
#endif
// NOTE: <time.h> intentionally not included: sketches often declare a global
// named "time", which would collide with time(). <sys/time.h>/GetSystemTime
// provide the current time without declaring any symbol named "time".

// FreeType is used to load Terminus's EMBEDDED bitmap strikes so the running
// sketch renders its default font exactly like the pixel-crisp editor font.
#include <ft2build.h>
#include FT_FREETYPE_H

// Embedded terminus font for fully self-contained exported binaries (no font
// file needed on disk). Included only when explicitly requested via
// PDEIDE_EMBEDDED_FONT; the live IDE build reads the .ttf from disk instead.
#ifdef PDEIDE_EMBEDDED_FONT
#include "terminus_ttf.h"
#endif

#ifndef PI
#define PI 3.14159265358979323846f
#endif
#ifndef TWO_PI
#define TWO_PI 6.28318530717958647692f
#endif
#ifndef TAU
#define TAU 6.28318530717958647692f
#endif
#ifndef HALF_PI
#define HALF_PI 1.57079632679489661923f
#endif
#ifndef QUARTER_PI
#define QUARTER_PI 0.78539816339744830961f
#endif

// Processing constants (mirror PValues from PConstants)
#define CODED 65535
#define UNDEFINED (-1)
// renderer names accepted by size(): raylib always renders accelerated 2D,
// so these only keep sketches that pass them to size() compiling
#define P2D 1
#define JAVA2D 1
#define P3D 2
#define OPENGL 2
#define PDF 3
enum {
  CORNER = 0,
  CORNERS = 1,
  RADIUS = 2,
  CENTER = 3
};
enum {
  LEFT = 37,
  UP = 38,
  RIGHT = 39,
  DOWN = 40
};
enum {
  BACKSPACE = 8,
  TAB = 9,
  ENTER = 13,
  RETURN = 13,
  ESC = 27,
  DELETE = 127
};
enum {
  OPEN = 0,
  CLOSE = 1,
  CHORD = 2,   // arc mode; shares Processing's global constant values with
  PIE = 3,     // POINTS / LINES below (real Processing uses the same ints)
  POINTS = 2,  // vertex modes
  LINES = 3,
  TRIANGLES = 4,
  TRIANGLE_STRIP = 5,
  TRIANGLE_FAN = 6,
  QUADS = 7,
  QUAD_STRIP = 8,
  POLYGON = 9
};
enum {
  ROUND = 100,   // stroke cap
  SQUARE = 101,  // stroke cap (butt)
  PROJECT = 102, // stroke cap (projecting)
  MITER = 103,   // stroke join
  BEVEL = 104    // stroke join
};

static int width = 814;
static int height = 576;
static int mouseX = 0;
static int mouseY = 0;
static int pmouseX = 0;
static int pmouseY = 0;
static int mouseWheelDelta = 0;
static bool mousePressed = false;

typedef Font PFont;
typedef RenderTexture2D PGraphics;

static PFont _currentFont;
static float _textSizeState = 12.0f;
static float _textSpacing = 1.0f;
static float _textLeading = 0.0f;
static Color _fillColor = { 255, 255, 255, 255 };
static Color _strokeColor = { 0, 0, 0, 255 };
static bool _useFill = true;
static bool _useStroke = true;
static float _strokeW = 1.0f;
static int _strokeCap = ROUND;
static int _strokeJoin = MITER;
static int _rectModeState = CORNER;
static int _ellipseModeState = CORNER;
static int _textAlignState = LEFT;

// input state ///////////////////////////////////////////////////////////
static char key = 0;
static int keyCode = 0;
static bool keyPressed = false;
static int mouseButton = UNDEFINED;

// loop control //////////////////////////////////////////////////////////
static bool _loopRunning = true;
static bool _redrawPending = true;   // draw() runs once even after setup noLoop()
static bool _exitRequested = false;
static bool _windowInit = false;
static bool _canvasInited = false;   // canvas cleared once at startup; afterwards
                                     // only an explicit background() clears it

// event callbacks (weak: defined only when the sketch provides them) /////
__attribute__((weak)) void keyPressed_event(void);
__attribute__((weak)) void keyReleased_event(void);
__attribute__((weak)) void mousePressed_event(void);
__attribute__((weak)) void mouseReleased_event(void);
__attribute__((weak)) void mouseMoved_event(void);
__attribute__((weak)) void mouseDragged_event(void);
__attribute__((weak)) void mouseWheel_event(void);

// time origin for millis() //////////////////////////////////////////////
static double _startTime = 0.0;

// perlin noise state /////////////////////////////////////////////////////
static int _noisePerm[512] = { 0 };
static int _noiseOctaves = 4;
static float _noiseFalloff = 0.5f;

// shape vertex buffer (beginShape / vertex / endShape) ///////////////////
#define _SHAPE_MAX_VERTS 8192
static Vector2 _shapeVerts[_SHAPE_MAX_VERTS];
static int _shapeVertCount = -1;
static int _shapeModeClose = 0;
static int _shapeMode = POLYGON;

static Font main_font;
static float current_text_size = 12.0f; 

static char _nfBuffers[4][64];
static int _nfBufferIndex = 0;
int frameCount = 0;

Color *pixels = NULL;
static Texture2D _pixelsTexture;
static RenderTexture2D _canvas;

// macros ////////////////////////////////////////////////////////////////
// str(..): with one arg, convert numbers to text and pass strings through;
// with multiple args act as TextFormat (printf) like Processing's str() does
// for "str()". Java str() semantics: str() grows the "String" abstraction.
static inline const char *_pde_str_num(double v) {
  static char _strNumBufs[4][64];
  static int _strNumIdx = 0;
  char *out = _strNumBufs[_strNumIdx];
  _strNumIdx = (_strNumIdx + 1) & 3;
  snprintf(out, 64, "%g", v);
  return out;
}
static inline const char *_pde_str_pass(const char *s) { return s; }
#define STR_CHOOSER(_1, _2, NAME, ...) NAME
#define str(...) STR_CHOOSER(__VA_ARGS__, str_format, str_one)(__VA_ARGS__)
#define str_one(A) _Generic((A), \
    const char *: _pde_str_pass, char *: _pde_str_pass, default: _pde_str_num)(A)
#define str_format(...) TextFormat(__VA_ARGS__)

// SIZE MACRO
#define SIZE_CHOOSER(_1, _2, _3, NAME, ...) NAME
#define size_converted(...) SIZE_CHOOSER(__VA_ARGS__, size3, size2, DUMMY)(__VA_ARGS__)

// BACKGROUND MACRO
#define BACKGROUND_CHOOSER(_1, _2, _3, _4, NAME, ...) NAME
#define background(...) BACKGROUND_CHOOSER(__VA_ARGS__, background4, background3, background2, background1, DUMMY)(__VA_ARGS__)

// FILL MACRO
#define FILL_CHOOSER(_1, _2, _3, _4, NAME, ...) NAME
#define fill(...) FILL_CHOOSER(__VA_ARGS__, fill4, fill3, fill2, fill1, DUMMY)(__VA_ARGS__)

// STROKE MACRO
#define STROKE_CHOOSER(_1, _2, _3, _4, NAME, ...) NAME
#define stroke(...) STROKE_CHOOSER(__VA_ARGS__, stroke4, stroke3, stroke2, stroke1, DUMMY)(__VA_ARGS__)

// abs: type-preserving so integer array subscripts stay integers. unsigned and
// long widen to long long so they don't fall through to the float default.
static inline int _pde_absi(int v) { return v < 0 ? -v : v; }
static inline long long _pde_absl(long long v) { return v < 0 ? -v : v; }
#define abs(X) _Generic((X), \
    int: _pde_absi, \
    unsigned int: _pde_absl, \
    long: _pde_absl, \
    unsigned long: _pde_absl, \
    long long: _pde_absl, \
    unsigned long long: _pde_absl, \
    default: fabsf)(X)

// COLOR PACKER (arity-routed): color(r,g,b), color(r,g,b,a), and single-arg
// forms pass through unpacked (hex literals arrive pre-packed by pde2c)
static inline uint32_t pack_color1(uint32_t packed) { return packed; }
static inline uint32_t pack_color2(float g, float a) {
  unsigned char gray = (unsigned char)g;
  return ((uint32_t)(unsigned char)a << 24) | ((uint32_t)gray << 16) |
         ((uint32_t)gray << 8) | gray;
}
static inline uint32_t pack_color3(float r, float g, float b) {
  return ((uint32_t)255 << 24) | ((uint32_t)(unsigned char)b << 16) |
         ((uint32_t)(unsigned char)g << 8) | (unsigned char)r;
}
static inline uint32_t pack_color4(float r, float g, float b, float a) {
  return ((uint32_t)(unsigned char)a << 24) | ((uint32_t)(unsigned char)b << 16) |
         ((uint32_t)(unsigned char)g << 8) | (unsigned char)r;
}
#define PCOLOR_CHOOSER(_1, _2, _3, _4, NAME, ...) NAME
#define pack_color(...) PCOLOR_CHOOSER(__VA_ARGS__, pack_color4, pack_color3, pack_color2, pack_color1)(__VA_ARGS__)

// LINE MACRO
#define LINE_CHOOSER(_1, _2, _3, _4, _5, _6, NAME, ...) NAME
#define line(...) LINE_CHOOSER(__VA_ARGS__, line6, dummy_error, line4, DUMMY)(__VA_ARGS__)

// PVECTOR MACRO
#define PV_CHOOSER(_1, _2, _3, NAME, ...) NAME
#define pvector(...) PV_CHOOSER(__VA_ARGS__, pvector3D, pvector2D, DUMMY)(__VA_ARGS__)

// RANDOM
#define RANDOM_CHOOSER(_1, _2, NAME, ...) NAME
#define random(...) RANDOM_CHOOSER(__VA_ARGS__, random2, random1)(__VA_ARGS__)

// NOISE
#define NOISE_CHOOSER(_1, _2, _3, NAME, ...) NAME
#define noise(...) NOISE_CHOOSER(__VA_ARGS__, noise3, noise2, noise1)(__VA_ARGS__)

// NOISE DETAIL
#define NOISEDETAIL_CHOOSER(_1, _2, NAME, ...) NAME
#define noiseDetail(...) NOISEDETAIL_CHOOSER(__VA_ARGS__, noiseDetail2, noiseDetail1)(__VA_ARGS__)

// SHAPE DISPATCH: beginShape()/endShape()/endShape(CLOSE)
// (zero-arg form resolved by pde2c -> beginShape0/endShape0)

// VERTEX
#define VERTEX_CHOOSER(_1, _2, _3, _4, _5, NAME, ...) NAME
#define vertex(...) VERTEX_CHOOSER(__VA_ARGS__, dummy_error, dummy_error, vertex3, vertex2)(__VA_ARGS__)

// DIST
#define DIST_CHOOSER(_1, _2, _3, _4, _5, _6, NAME, ...) NAME
#define dist(...) DIST_CHOOSER(__VA_ARGS__, dist3, dummy_error, dist2, dummy_error)(__VA_ARGS__)

// MAG
#define MAG_CHOOSER(_1, _2, _3, NAME, ...) NAME
#define mag(...) MAG_CHOOSER(__VA_ARGS__, mag3, mag, DUMMY)(__VA_ARGS__)

// LOADFONT: loadFont(file) uses a default size; glyph size comes from
// _textSizeState at draw time (DrawTextEx scales), so the base size is free
#define LOADFONT_CHOOSER(_1, _2, NAME, ...) NAME
#define loadFont(...) LOADFONT_CHOOSER(__VA_ARGS__, loadFont2, loadFont1)(__VA_ARGS__)

// TEXTFONT: textFont(font) / textFont(font, size)
#define TEXTFONT_CHOOSER(_1, _2, NAME, ...) NAME
#define textFont(...) TEXTFONT_CHOOSER(__VA_ARGS__, textFont2, textFont1)(__VA_ARGS__)


// random ///////////////////////////////////////////////////////////////////////////////////////////////

static inline float random1(float max) {
    return ((float)rand() / (float)RAND_MAX) * max;
}

static inline float random2(float min, float max) {
  return min + ((float)rand() / (float)RAND_MAX) * (max - min);
}

// forward declarations for the noise engine
static void _noiseReshuffle(void);
static float noise1(float x);
static float noise2(float x, float y);
static float noise3(float x, float y, float z);
static float lerp(float start, float stop, float amt);

static inline void randomSeed(unsigned long seed) {
  srand((unsigned int)seed);
  _noiseReshuffle();
}

static float randomGaussian(void) {
  // Box-Muller transform
  static bool hasSpare = false;
  static float spare = 0.0f;
  if (hasSpare) {
    hasSpare = false;
    return spare;
  }
  float u, v, s;
  do {
    u = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
    v = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
    s = u * u + v * v;
  } while (s >= 1.0f || s == 0.0f);
  float mul = sqrtf(-2.0f * logf(s) / s);
  spare = v * mul;
  hasSpare = true;
  return u * mul;
}

// perlin noise (Ken Perlin improved noise, fBm octaves) //////////////////

static void _noiseReshuffle(void) {
  for (int i = 0; i < 256; i++) _noisePerm[i] = i;
  for (int i = 255; i > 0; i--) {
    int j = rand() % (i + 1);
    int tmp = _noisePerm[i];
    _noisePerm[i] = _noisePerm[j];
    _noisePerm[j] = tmp;
  }
  for (int i = 0; i < 256; i++) _noisePerm[256 + i] = _noisePerm[i];
}

static const float _noiseGrad(int hash, float x, float y, float z) {
  int h = hash & 15;
  float u = (h < 8) ? x : y;
  float v = (h < 4) ? y : ((h == 12 || h == 14) ? x : z);
  return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

static const float _noiseFade(float t) {
  return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static float _noise3Raw(float x, float y, float z) {
  int X = ((int)x) & 255;
  int Y = ((int)y) & 255;
  int Z = ((int)z) & 255;
  x -= floorf(x); y -= floorf(y); z -= floorf(z);
  float u = _noiseFade(x), v = _noiseFade(y), w = _noiseFade(z);

  int A = _noisePerm[X] + Y, AA = _noisePerm[A] + Z, AB = _noisePerm[A + 1] + Z;
  int B = _noisePerm[X + 1] + Y, BA = _noisePerm[B] + Z, BB = _noisePerm[B + 1] + Z;

  return lerp(w,
    lerp(v,
      lerp(u, _noiseGrad(_noisePerm[AA],     x,     y,     z),
              _noiseGrad(_noisePerm[BA], x - 1.0f,     y,     z)),
      lerp(u, _noiseGrad(_noisePerm[AB],     x, y - 1.0f,     z),
              _noiseGrad(_noisePerm[BB], x - 1.0f, y - 1.0f,     z))),
    lerp(v,
      lerp(u, _noiseGrad(_noisePerm[AA + 1],     x,     y, z - 1.0f),
              _noiseGrad(_noisePerm[BA + 1], x - 1.0f,     y, z - 1.0f)),
      lerp(u, _noiseGrad(_noisePerm[AB + 1],     x, y - 1.0f, z - 1.0f),
              _noiseGrad(_noisePerm[BB + 1], x - 1.0f, y - 1.0f, z - 1.0f))));
}

static float noise1(float x) {
  return noise3(x, 0.0f, 0.0f);
}

static float noise2(float x, float y) {
  return noise3(x, y, 0.0f);
}

static float noise3(float x, float y, float z) {
  if (_noisePerm[0] == _noisePerm[1] && _noisePerm[0] == 0) _noiseReshuffle();
  float sum = 0.0f;
  float amp = 1.0f;
  float freq = 1.0f;
  float norm = 0.0f;
  for (int i = 0; i < _noiseOctaves; i++) {
    sum += amp * _noise3Raw(x * freq, y * freq, z * freq);
    norm += amp;
    amp *= _noiseFalloff;
    freq *= 2.0f;
  }
  return sum / norm * 0.5f + 0.5f;
}

static inline void noiseDetail1(int lod) {
  _noiseOctaves = lod;
}

static inline void noiseDetail2(int lod, float falloff) {
  _noiseOctaves = lod;
  _noiseFalloff = falloff;
}

// noiseSeed: reseed Perlin permutation only (does not touch rand()),
// matching Processing's separate noise seed state.
static inline void noiseSeed(unsigned long seed) {
  for (int i = 0; i < 256; i++) _noisePerm[i] = i;
  unsigned int s = (unsigned int)seed;
  for (int i = 255; i > 0; i--) {
    s = s * 1103515245u + 12345u;
    unsigned int j = (s >> 16) % (unsigned int)(i + 1);
    int tmp = _noisePerm[i];
    _noisePerm[i] = _noisePerm[j];
    _noisePerm[j] = tmp;
  }
  for (int i = 0; i < 256; i++) _noisePerm[256 + i] = _noisePerm[i];
}

// PFont ////////////////////////////////////////////////////////////////

static inline PFont loadFont2(const char *filename, int fontSize) {
  PFont font = LoadFontEx(filename, fontSize, NULL, 0);
  SetTextureFilter(font.texture, TEXTURE_FILTER_POINT);
  return font;
}

// Processing's loadFont(file): vlw carries its own size; any sane base works
// here since text() renders at _textSizeState via DrawTextEx scaling
#define LOADFONT_DEFAULT_SIZE 12
static inline PFont loadFont1(const char *filename) {
  return loadFont2(filename, LOADFONT_DEFAULT_SIZE);
}

static inline PFont createFont(const char *filename, int fontSize) {  return loadFont2(filename, fontSize);
}

// Processing's createFont(name, size, smooth) — smooth flag ignored
static inline PFont createFont3(const char *filename, int fontSize, int smooth) {
  (void)smooth;
  return loadFont2(filename, fontSize);
}

// colorMode: only RGB is modeled; calls are accepted and ignored
#define RGB 1
#define HSB 2
static inline void colorMode1(int mode) { (void)mode; }
static inline void colorMode2(int mode, float max1) { (void)mode; (void)max1; }
static inline void colorMode3(int mode, float max1, float max2) { (void)mode; (void)max1; (void)max2; }
static inline void colorMode4(int mode, float max1, float max2, float max3) { (void)mode; (void)max1; (void)max2; (void)max3; }

// Java-style modulo: works on floats like Processing's % (int math identical)
static inline float _pde_modf(float a, float b) {
  if (b == 0.0f) return 0.0f;
  float r = fmodf(a, b);
  // Processing keeps the divisor's sign convention (like C fmod); Java % on
  // negatives matches fmod, so no extra fixup needed.
  return r;
}

static inline int _pde_modi(int a, int b) {
  return b != 0 ? a % b : 0;
}

// type-preserving: int % int stays an integer (array subscripts etc.)
#define pmod(A, B) _Generic((A), int: _pde_modi, default: _pde_modf)(A, B)

// anti-aliasing toggles: raylib draws smoothed by default; accepted and ignored
static inline void smooth(void) {}
static inline void smooth1(int level) { (void)level; }
static inline void noSmooth(void) {}

// pixelDensity: display density not modeled; reads return 1
static inline int pixelDensity(void) { return 1; }
static inline void pixelDensity1(int d) { (void)d; }
static inline int displayDensity(void) { return 1; }
static inline void displayDensity1(int d) { (void)d; }

static inline void textFont1(PFont font) {
  _currentFont = font;
}

// Processing's textFont(font, size): selects the font and sets the draw size
static inline void textFont2(PFont font, float size) {
  _currentFont = font;
  _textSizeState = size;
}

static inline void textSize(float size) {
  _textSizeState = size;
}

static inline void textAlign1(int alignX) {
  _textAlignState = alignX;
}

static inline void textAlign2(int alignX, int alignY) {
  (void)alignY;
  _textAlignState = alignX;
}

static inline float textWidth(const char *textStr) {
  return MeasureTextEx(_currentFont, textStr, (float)((int)_textSizeState), _textSpacing).x;
}

// textLeading: line-to-line spacing (defaults to the font base size)
static inline void textLeading(float leading) {
  _textLeading = leading;
}

static inline float textAscent(void) {
  if (_currentFont.baseSize > 0) return (float)_currentFont.baseSize;
  return _textSizeState * 0.8f;
}

static inline float textDescent(void) {
  if (_currentFont.baseSize > 0) return (float)_currentFont.baseSize * 0.2f;
  return _textSizeState * 0.2f;
}

static inline void text_str(const char *format, float x, float y, ...) {
    if (!_useFill) return;

    static char text_buffer[1024];

    va_list args;
    va_start(args, y);

    vsnprintf(text_buffer, sizeof(text_buffer), format, args);

    va_end(args);

    float offsetX = 0.0f;
    if (_textAlignState == RIGHT) {
      offsetX = -MeasureTextEx(_currentFont, text_buffer, (float)((int)_textSizeState), _textSpacing).x;
    } else if (_textAlignState == CENTER) {
      offsetX = -MeasureTextEx(_currentFont, text_buffer, (float)((int)_textSizeState), _textSpacing).x * 0.5f;
    }

    DrawTextEx(_currentFont, text_buffer, (Vector2){ (int)(x + offsetX), (int)y }, (float)((int)_textSizeState), _textSpacing, _fillColor);
}

// text(number, x, y): Processing prints numeric values directly
static inline void textNum(double v, float x, float y) {
  char buf[64];
  if (v == (long long)v && fabs(v) < 1e15)
    snprintf(buf, sizeof(buf), "%lld", (long long)v);
  else
    snprintf(buf, sizeof(buf), "%g", v);
  text_str(buf, x, y);
}

// text() dispatches: string literals/pointers -> text_str (printf-style),
// numeric expressions -> textNum (Processing prints numbers as-is)
#define text(a, ...) \
  _Generic((a), \
    const char *: text_str, \
    char *: text_str, \
    default: textNum)((a), ##__VA_ARGS__)

// print ////////////////////////////////////////////////////////////////////////////

static inline void print(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    fflush(stdout);
}

static inline void _pde_println_vfmt(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

static inline void _pde_print_str(const char *s) { printf("%s\n", s); fflush(stdout); }
static inline void _pde_print_num(double v) { printf("%g\n", v); fflush(stdout); }

// println(): Processor-style 0/1/multi arg dispatch. Single numeric args are
// printed directly; single strings pass through; multi-arg acts as printf.
#define PRINTLN_CHOOSER(_1, _2, _3, _4, NAME, ...) NAME
#define println(...) PRINTLN_CHOOSER(0, __VA_ARGS__, _pde_println_fmt, _pde_println_fmt, _pde_println_one, _pde_println_none)(__VA_ARGS__)
#define _pde_println_none() (void)printf("\n")
#define _pde_println_one(v) _Generic((v), \
    const char *: _pde_print_str, char *: _pde_print_str, default: _pde_print_num)(v)
#define _pde_println_fmt(...) _pde_println_vfmt(__VA_ARGS__)

static inline void _pde_print_strl(const char *s) { printf("%s", s); fflush(stdout); }
static inline void _pde_print_numl(double v) { printf("%g", v); fflush(stdout); }
static inline void _pde_print_vfmt(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    fflush(stdout);
}

// print(): single numeric/string arg prints directly; multi-arg acts as printf.
#define PRINT_CHOOSER(_1, _2, _3, NAME, ...) NAME
#define print(...) PRINT_CHOOSER(0, __VA_ARGS__, _pde_print_fmt, _pde_print_one)(__VA_ARGS__)
#define _pde_print_one(v) _Generic((v), \
    const char *: _pde_print_strl, char *: _pde_print_strl, default: _pde_print_numl)(v)
#define _pde_print_fmt(...) _pde_print_vfmt(__VA_ARGS__)

///////////////////////////////////////////////////////////////////////////////////////////

/* Load a font from its EMBEDDED bitmap strikes (the authentic hand-drawn pixel
 * glyphs) via FreeType, ports identical to the IDE's font renderer so the
 * running sketch's text matches the editor. Renders monospace glyphs as crisp
 * 1-bit pixels. Falls back to the default font when no strike matches.
 * `path` is tried first; if unreadable, the absolute IDE path is used, then
 * raylib's default font. */
static inline Font load_pixel_font(const char *path, int px) {
  Font f = {0};
  unsigned char *data = NULL;
  unsigned long sz = 0;
  int free_data = 0;

  /* Author-supplied path (or the stock terminus path) is tried first so custom
   * fonts keep working. If nothing reads from disk we fall back to the
   * embedded terminus bitmap-strike font below, which lets fully
   * self-contained exported binaries render text with no font file present. */
  {
    const char *candidates[] = { path, "/usr/share/fonts/TTF/terminus.ttf", NULL };
    FILE *fp = NULL;
    for (int ci = 0; ci < 2 && !fp; ci++) {
      if (!candidates[ci] || !candidates[ci][0]) continue;
      FILE *t = fopen(candidates[ci], "rb");
      if (t) { fp = t; break; }
    }
    /* also try the absolute IDE location if the relative path was a dead end */
    if (!fp) {
      const char *abs = "/home/kof/src/RaylibProcessing/terminus.ttf";
      if (access(abs, R_OK) == 0) fp = fopen(abs, "rb");
    }
    if (fp) {
      fseek(fp, 0, SEEK_END); long t = ftell(fp); fseek(fp, 0, SEEK_SET);
      if (t > 0) {
        data = malloc((size_t)t);
        if (data) {
          size_t got = fread(data, 1, (size_t)t, fp);
          if (got == (size_t)t) { sz = (unsigned long)t; free_data = 1; }
          else { free(data); data = NULL; }
        }
      }
      fclose(fp);
    }
  }

  if (!data) {
#ifdef PDEIDE_EMBEDDED_FONT
    data = (unsigned char *)terminus_ttf;
    sz = terminus_ttf_len;
#endif
  }

  if (!data) return GetFontDefault();

  FT_Library lib = NULL;
  FT_Face face = NULL;
  if (FT_Init_FreeType(&lib) != 0) { if (free_data) free(data); return GetFontDefault(); }
  if (FT_New_Memory_Face(lib, data, (FT_Long)sz, 0, &face) != 0) {
    FT_Done_FreeType(lib); if (free_data) free(data); return GetFontDefault();
  }

  /* select the embedded strike whose ppem matches px; else first reaching px */
  int chosen = -1;
  for (int i = 0; i < face->num_fixed_sizes; i++) {
    FT_Bitmap_Size bs = face->available_sizes[i];
    int ppem = (int)(((bs.y_ppem > 0 ? bs.y_ppem : bs.height) + 32) / 64);
    if (ppem == px) { chosen = i; break; }
    if (chosen < 0 && ppem > px) chosen = i;
  }
  if (chosen < 0 || FT_Select_Size(face, chosen) != 0) {
    FT_Done_FreeType(lib); if (free_data) free(data); return GetFontDefault();
  }

  int ascent = (int)(face->size->metrics.ascender / 64);
  int cps[] = {
    32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,
    58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,
    91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,
    117,118,119,120,121,122,123,124,125,126 };
  int ncp = (int)(sizeof(cps)/sizeof(cps[0]));
  GlyphInfo *glyphs = calloc((size_t)ncp, sizeof(GlyphInfo));
  Rectangle *recs = NULL;
  int gcount = 0;

  for (int i = 0; i < ncp; i++) {
    int cp = cps[i];
    FT_UInt gid = FT_Get_Char_Index(face, (FT_ULong)cp);
    if (gid == 0) continue;
    if (FT_Load_Glyph(face, gid, FT_LOAD_RENDER) != 0) continue;
    FT_Bitmap *b = &face->glyph->bitmap;
    int bw = (int)b->width, bh = (int)b->rows;
    int stride = (int)b->pitch;
    if (stride < 0) stride = -stride;

    if (cp == 32 && (bw == 0 || bh == 0)) {
      int a = (int)(face->glyph->advance.x/64);
      Image sp = { .data = calloc((size_t)(a>0?a:px)* (size_t)px, 1),
                   .width = a>0?a:px, .height = px, .mipmaps = 1,
                   .format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE };
      glyphs[gcount].value = cp; glyphs[gcount].advanceX = a>0?a:px;
      glyphs[gcount].offsetX = 0; glyphs[gcount].offsetY = 0; glyphs[gcount].image = sp;
      gcount++; continue;
    }
    if (bw <= 0 || bh <= 0) continue;

    unsigned char *pxd = calloc((size_t)bw*(size_t)bh, 1);
    const unsigned char *src = b->buffer;
    for (int y = 0; y < bh; y++) {
      const unsigned char *row = src + (long)y*stride;
      for (int x = 0; x < bw; x++) {
        pxd[(size_t)y*bw + (size_t)x] = (row[x>>3] >> (7-(x&7))) & 1 ? 255 : 0;
      }
    }
    Image gi = { .data = pxd, .width = bw, .height = bh, .mipmaps = 1,
                 .format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE };
    glyphs[gcount].value = cp;
    glyphs[gcount].offsetX = face->glyph->bitmap_left;
    glyphs[gcount].offsetY = ascent - face->glyph->bitmap_top;
    glyphs[gcount].advanceX = (int)(face->glyph->advance.x/64);
    glyphs[gcount].image = gi;
    gcount++;
  }

  FT_Done_FreeType(lib);
  if (free_data) free(data);

  if (gcount <= 0) { free(glyphs); return GetFontDefault(); }

  Image atlas = GenImageFontAtlas(glyphs, &recs, gcount, px, 0, 0);
  if (atlas.data == NULL) { for (int i=0;i<gcount;i++) UnloadImage(glyphs[i].image); free(glyphs); return GetFontDefault(); }

  f.baseSize = px;
  f.glyphCount = gcount;
  f.glyphPadding = 0;
  f.texture = LoadTextureFromImage(atlas);
  f.recs = recs;
  f.glyphs = glyphs;
  UnloadImage(atlas);
  return f;
}

static inline void load_default_font(void) {
  main_font = load_pixel_font("terminus.ttf", (int)current_text_size);

  if (main_font.texture.id <= 0) main_font = GetFontDefault();

  _currentFont = main_font;
  SetTextureFilter(main_font.texture, TEXTURE_FILTER_POINT);
}

// size /////////////////////////////////////////////////////////
static inline void size3(int w, int h, const char *title) {
  InitWindow(w, h, title);
  load_default_font();
  SetTargetFPS(60);

  // Processing accepts polygons in any winding -> never backface-cull
  rlDisableBackfaceCulling();

  width = w;
  height = h;
  _startTime = GetTime();

  pixels = (Color *)MemAlloc(w * h * sizeof(Color));
  Image blank = GenImageColor(w, h, BLANK);
  _pixelsTexture = LoadTextureFromImage(blank);
  UnloadImage(blank);

  // offscreen canvas all drawing renders into (Processing PGraphics model)
  _canvas = LoadRenderTexture(w, h);
  _windowInit = true;
}

static inline void size2(int w, int h) {
  size3(w, h, str("Processing Ray %s", _VERSION_) );
}


// background ////////////////////////////////////////////
static Color current_background_color = { 200, 200, 200, 255 };

static inline void background4(float r, float g, float b, float a) {
  current_background_color = (Color){ (unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a };
  ClearBackground(current_background_color);
}

static inline void background3(float r, float g, float b) {
  background4(r, g, b, 255.0f);
}

static inline void background2(float grayOrColor, float alpha) {
  if (grayOrColor > 255.0f) { // packed ABGR + alpha (Processing: background(rgb, a))
    uint32_t c = (uint32_t)grayOrColor;
    background4((float)(c & 0xFF), (float)((c >> 8) & 0xFF), (float)((c >> 16) & 0xFF), alpha);
  } else {
    background4(grayOrColor, grayOrColor, grayOrColor, alpha);
  }
}

static inline void background1(uint32_t c) {
  if (c <= 255) {
    background4(c, c, c, 255);
  } else {
    uint8_t r = c         & 0xFF;
    uint8_t g = (c >> 8)  & 0xFF;
    uint8_t b = (c >> 16) & 0xFF;
    background4(r, g, b, 255);
  }
}

// loop control /////////////////////////////////////////////////////////////////////////

static inline void noLoop(void) { _loopRunning = false; }
static inline void loop(void) { _loopRunning = true; }
static inline void redraw(void) { _redrawPending = true; }
static inline void exit_sketch(void) { _exitRequested = true; }

static inline int millis(void) {
  return (int)((GetTime() - _startTime) * 1000.0);
}

// date/time (no <time.h>: see include note above).
// Days-from-civil algorithm (Howard Hinnant) converts epoch seconds to Y/M/D.
static void _civilFromEpoch(long long days, int *y, int *m, int *d) {
  days += 719468;
  long long era = (days >= 0 ? days : days - 146096) / 146097;
  unsigned doe = (unsigned)(days - era * 146097);
  unsigned yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
  long long yy = (long long)yoe + era * 400;
  unsigned doy = doe - (365*yoe + yoe/4 - yoe/100);
  unsigned mp = (5*doy + 2)/153;
  *d = (int)(doy - (153*mp+2)/5 + 1);
  *m = (int)(mp < 10 ? mp+3 : mp-9);
  if (*m <= 2) yy++;
  *y = (int)yy;
}

typedef struct { int sec, minute, hour, day, month, year; } _PdeDateTime;

#ifdef PROCESSING_WIN32
/* seconds since the UNIX epoch via the 1601-based FILETIME clock */
static long long _winEpochSec(void) {
  FILETIME ft; GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER u;
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  return (long long)((u.QuadPart / 10000000LL) - 11644473600LL);
}
#endif

static _PdeDateTime _nowParts(void) {
  long long secs;
#ifdef PROCESSING_WIN32
  secs = _winEpochSec();
#else
  struct timeval tv;
  gettimeofday(&tv, NULL);
  secs = (long long)tv.tv_sec;
#endif
  long long days = secs / 86400;
  long long rem = secs % 86400;
  if (rem < 0) { rem += 86400; days--; }
  _PdeDateTime p;
  p.sec = (int)(rem % 60);
  p.minute = (int)((rem / 60) % 60);
  p.hour = (int)(rem / 3600);
  _civilFromEpoch(days, &p.year, &p.month, &p.day);
  return p;
}

static inline int second(void) {
  return _nowParts().sec;
}

static inline int minute(void) {
  return _nowParts().minute;
}

static inline int hour(void) {
  return _nowParts().hour;
}

static inline int day(void) {
  return _nowParts().day;
}

static inline int month(void) {
  return _nowParts().month;
}

static inline int year(void) {
  return _nowParts().year;
}

static float current_frame_rate = 60.0f;

static inline void set_frame_rate(float fps) {
  current_frame_rate = fps;
  if (fps > 0.0f) SetTargetFPS((int)(fps + 0.5f));
}

// input event pump /////////////////////////////////////////////////////////////////////
// Maps raylib key events onto Processing's key/keyCode/keyPressed globals and
// fires the weak *_event callbacks when the sketch defines them.

static const struct { int rlKey; int procCode; } _specialKeys[] = {
  { KEY_RIGHT,     RIGHT },
  { KEY_LEFT,      LEFT },
  { KEY_DOWN,      DOWN },
  { KEY_UP,        UP },
  { KEY_ENTER,     ENTER },
  { KEY_KP_ENTER,  ENTER },
  { KEY_ESCAPE,    ESC },
  { KEY_BACKSPACE, BACKSPACE },
  { KEY_DELETE,    DELETE },
  { KEY_TAB,       TAB },
};
#define _SPECIAL_KEY_COUNT (int)(sizeof(_specialKeys) / sizeof(_specialKeys[0]))

static inline void _pumpEvents(void) {
  // keyboard state: any key currently held
  bool held = false;
  for (int k = 32; k < 350; k++) if (IsKeyDown(k)) { held = true; break; }
  keyPressed = held;

  // edge-triggered presses -> key/keyCode globals + keyPressed_event()
  bool pressedThisFrame = false;
  for (int i = 0; i < _SPECIAL_KEY_COUNT; i++) {
    if (IsKeyPressed(_specialKeys[i].rlKey)) {
      keyCode = _specialKeys[i].procCode;
      key = (_specialKeys[i].procCode < 128) ? (char)_specialKeys[i].procCode : CODED;
      pressedThisFrame = true;
    }
  }
  int ch = GetCharPressed();
  while (ch > 0) {
    if (ch < 128) key = (char)ch;
    keyCode = ch;
    pressedThisFrame = true;
    ch = GetCharPressed();
  }

  // raylib reports printable keys via IsKeyPressed too, catch non-queued ones
  for (int k = 32; k < 127; k++) {
    if (IsKeyPressed(k)) {
      key = (char)k;
      keyCode = k;
      pressedThisFrame = true;
    }
  }

  if (pressedThisFrame && keyPressed_event) keyPressed_event();

  // edge-triggered releases
  bool releasedThisFrame = false;
  for (int k = 32; k < 350; k++) if (IsKeyReleased(k)) { releasedThisFrame = true; break; }
  if (releasedThisFrame && keyReleased_event) keyReleased_event();

  // mouse buttons
  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))   { mouseButton = LEFT;   if (mousePressed_event) mousePressed_event(); }
  if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))  { mouseButton = RIGHT;  if (mousePressed_event) mousePressed_event(); }
  if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) { mouseButton = CENTER; if (mousePressed_event) mousePressed_event(); }
  if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) ||
      IsMouseButtonReleased(MOUSE_BUTTON_RIGHT) ||
      IsMouseButtonReleased(MOUSE_BUTTON_MIDDLE)) {
    if (mouseReleased_event) mouseReleased_event();
  }

  // mouse movement: Moved when no button held, Dragged while held
  int mx = GetMouseX(), my = GetMouseY();
  if (mx != pmouseX || my != pmouseY) {
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) ||
        IsMouseButtonDown(MOUSE_BUTTON_RIGHT) ||
        IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
      if (mouseDragged_event) mouseDragged_event();
    } else {
      if (mouseMoved_event) mouseMoved_event();
    }
  }

  // wheel
  int wheelMove = (int)GetMouseWheelMove();
  if (wheelMove != 0) {
    mouseWheelDelta = wheelMove;
    if (mouseWheel_event) mouseWheel_event();
  } else {
    mouseWheelDelta = 0;
  }
}

static inline void beginDraw(void) {
  // a sketch may never call size() (e.g. empty setup): give it a real window
  if (_canvas.id == 0) size3(640, 480, "Processing Ray");
  width = GetScreenWidth();
  height = GetScreenHeight();
  pmouseX = mouseX;
  pmouseY = mouseY;
  mouseX = GetMouseX();
  mouseY = GetMouseY();
  mousePressed = IsMouseButtonDown(MOUSE_BUTTON_LEFT);

  _pumpEvents();

  BeginDrawing();

  // all sketch drawing goes into the offscreen canvas. The canvas is wiped
  // once at startup (to the current background color rather than leave
  // uninitialized texture memory); after that it is cleared only when the
  // sketch itself calls background() (Processing semantics: no background()
  // call means no background drawn, prior frames persist).
  BeginTextureMode(_canvas);
  if (!_canvasInited) {
    ClearBackground(current_background_color);
    _canvasInited = true;
  }
}

static inline void endDraw(void) {
  EndTextureMode();

  // blit canvas to the window (RT textures are y-flipped)
  DrawTextureRec(_canvas.texture,
                 (Rectangle){ 0, 0, (float)_canvas.texture.width, -(float)_canvas.texture.height },
                 (Vector2){ 0, 0 }, WHITE);

  EndDrawing();
  frameCount++;
}

// save ////////////////////////////////////////////////////////////////////

// capture the sketch canvas, not the window backbuffer: TakeScreenshot() is
// immediate in raylib 6 and would glReadPixels while draw calls still sit in
// the un-flushed rlgl batch (yielding a background-only image)
static inline void _exportCanvas(const char *filename) {
  rlDrawRenderBatchActive();               // push queued draws into _canvas
  Image img = LoadImageFromTexture(_canvas.texture);
  ImageFlipVertical(&img);                 // RT textures are y-flipped
  ExportImage(img, filename);
  UnloadImage(img);
}

static inline void save(const char *filename) {
  _exportCanvas(filename);
}

static inline void saveFrame(const char *filename) {
  const char *target = filename ? filename : "screen-####.png";
  static char buffer[256];
  int b_idx = 0;
  int num_pads = 0;
  for (int i = 0; target[i] != '\0' && b_idx < 250; i++) {
    if (target[i] == '#') {
      num_pads++;
      if (target[i + 1] != '#') {
        b_idx += snprintf(&buffer[b_idx], 256 - b_idx, "%0*d", num_pads, frameCount);
        num_pads = 0;
      }
    } else {
      buffer[b_idx++] = target[i];
    }
  }
  buffer[b_idx] = '\0';
  _exportCanvas(buffer);
}

// nf /////////////////////////////////////////////////////////////

static inline const char* nfInt(int value, int digits) {
  char *buf = _nfBuffers[_nfBufferIndex];
  _nfBufferIndex = (_nfBufferIndex + 1) % 4;
  snprintf(buf, 64, "%0*d", digits, value);
  return buf;
}

static inline const char* nfFloat(float value, int left, int right) {
  char *buf = _nfBuffers[_nfBufferIndex];
  _nfBufferIndex = (_nfBufferIndex + 1) % 4;
  char temp[64];
  snprintf(temp, 64, "%.*f", right, (value < 0) ? -value : value);
  int dot_pos = 0;
  while (temp[dot_pos] != '.' && temp[dot_pos] != '\0') {
    dot_pos++;
  }
  int missing = left - dot_pos;
  int idx = 0;
  if (value < 0) {
    buf[idx++] = '-';
  }
  for (int i = 0; i < missing; i++) {
    buf[idx++] = '0';
  }
  for (int i = 0; temp[i] != '\0'; i++) {
    buf[idx++] = temp[i];
  }
  buf[idx] = '\0';
  return buf;
}

// nf / nfs / nfp / nfc (arity + numeric type dispatch) //////////////////
// nf: zero-pad to given digit count. nfp: like nf with explicit sign for
// positives. nfs: no padding, but explicit sign. nfc: comma grouping.
static inline const char *_pde_numfmt(double v, int left, int right,
                                      int padFlag, int commaFlag, int signFlag) {
  static char bufs[4][96];
  static int idx = 0;
  char *buf = bufs[idx];
  idx = (idx + 1) & 3;
  double av = (v < 0) ? -v : v;
  char temp[64];
  if (padFlag) snprintf(temp, sizeof temp, "%.*f", right, av);
  else if (right > 0) snprintf(temp, sizeof temp, "%.*f", right, av);
  else if (commaFlag) snprintf(temp, sizeof temp, "%.0f", av);
  else snprintf(temp, sizeof temp, "%g", av);
  const char *dot = strchr(temp, '.');
  size_t il = dot ? (size_t)(dot - temp) : strlen(temp);
  char intpart[40], frac[20];
  memcpy(intpart, temp, il);
  intpart[il] = '\0';
  strcpy(frac, dot ? dot : "");
  int missing = left - (int)il;
  if (missing < 0) missing = 0;
  int i = 0;
  if (signFlag && v >= 0) buf[i++] = '+';
  else if (v < 0) buf[i++] = '-';
  for (int k = 0; k < missing; k++) buf[i++] = '0';
  if (commaFlag) {
    int indent = (int)strlen(intpart) % 3;
    if (indent == 0) indent = 3;
    for (int k = 0; k < indent; k++) buf[i++] = intpart[k];
    for (int k = indent; intpart[k]; k++) {
      if ((k - indent) % 3 == 0) buf[i++] = ',';
      buf[i++] = intpart[k];
    }
  } else {
    for (int k = 0; intpart[k]; k++) buf[i++] = intpart[k];
  }
  for (int k = 0; frac[k]; k++) buf[i++] = frac[k];
  buf[i] = '\0';
  return buf;
}

#define _NF_CHOOSER(_1, _2, _3, NAME, ...) NAME
#define nf(...) _NF_CHOOSER(__VA_ARGS__, _pde_nf3, _pde_nf2, _pde_nf1)(__VA_ARGS__)
#define _pde_nf1(v) _Generic((v), float: _pde_nf_f1, double: _pde_nf_f1, default: _pde_nf_f1)(v)
static inline const char *_pde_nf_f1(double v) { return _pde_numfmt(v, 0, 0, 0, 0, 0); }
#define _pde_nf2(v, d) _Generic((v), float: _pde_nf_f2, double: _pde_nf_f2, default: _pde_nf_i2)(v, d)
static inline const char *_pde_nf_f2(double v, int d) { return _pde_numfmt(v, 0, d, 0, 0, 0); }
static inline const char *_pde_nf_i2(double v, int d) { return _pde_numfmt(v, d, 0, 1, 0, 0); }
static inline const char *_pde_nf3(double v, int l, int r) { return _pde_numfmt(v, l, r, 1, 0, 0); }

#define _NFS_CHOOSER(_1, _2, _3, NAME, ...) NAME
#define nfs(...) _NFS_CHOOSER(__VA_ARGS__, _pde_nfs3, _pde_nfs2, _pde_nfs1)(__VA_ARGS__)
#define _pde_nfs1(v) _Generic((v), float: _pde_nfs_f1, double: _pde_nfs_f1, default: _pde_nfs_f1)(v)
static inline const char *_pde_nfs_f1(double v) { return _pde_numfmt(v, 0, 0, 0, 0, 1); }
#define _pde_nfs2(v, d) _Generic((v), float: _pde_nfs_f2, double: _pde_nfs_f2, default: _pde_nfs_i2)(v, d)
static inline const char *_pde_nfs_f2(double v, int d) { return _pde_numfmt(v, 0, d, 0, 0, 1); }
static inline const char *_pde_nfs_i2(double v, int d) { (void)d; return _pde_numfmt(v, 0, 0, 0, 0, 1); }
static inline const char *_pde_nfs3(double v, int l, int r) { (void)l; return _pde_numfmt(v, 0, r, 0, 0, 1); }

#define _NFP_CHOOSER(_1, _2, _3, NAME, ...) NAME
#define nfp(...) _NFP_CHOOSER(__VA_ARGS__, _pde_nfp3, _pde_nfp2, _pde_nfp1)(__VA_ARGS__)
#define _pde_nfp1(v) _Generic((v), float: _pde_nfp_f1, double: _pde_nfp_f1, default: _pde_nfp_f1)(v)
static inline const char *_pde_nfp_f1(double v) { return _pde_numfmt(v, 0, 0, 1, 0, 1); }
#define _pde_nfp2(v, d) _Generic((v), float: _pde_nfp_f2, double: _pde_nfp_f2, default: _pde_nfp_i2)(v, d)
static inline const char *_pde_nfp_f2(double v, int d) { return _pde_numfmt(v, 0, d, 1, 0, 1); }
static inline const char *_pde_nfp_i2(double v, int digits) { return _pde_numfmt(v, digits, 0, 1, 0, 1); }
static inline const char *_pde_nfp3(double v, int l, int r) { return _pde_numfmt(v, l, r, 1, 0, 1); }

#define _NFC_CHOOSER(_1, _2, NAME, ...) NAME
#define nfc(...) _NFC_CHOOSER(__VA_ARGS__, _pde_nfc2, _pde_nfc1)(__VA_ARGS__)
#define _pde_nfc1(v) _Generic((v), float: _pde_nfc_f1, double: _pde_nfc_f1, default: _pde_nfc_f1)(v)
static inline const char *_pde_nfc_f1(double v) { return _pde_numfmt(v, 0, 0, 0, 1, 0); }
#define _pde_nfc2(v, d) _Generic((v), float: _pde_nfc_f2, double: _pde_nfc_f2, default: _pde_nfc_f2)(v, d)
static inline const char *_pde_nfc_f2(double v, int d) { return _pde_numfmt(v, 0, d, 0, 1, 0); }

// string concat / charAt (rotating static buffers, same trick as nf) /////
static const char *_pde_numf(double v) {
  static char bufs[4][64];
  static int idx = 0;
  char *out = bufs[idx];
  idx = (idx + 1) & 3;
  snprintf(out, 64, "%g", v);
  return out;
}

static const char *_pde_cat(const char *a, const char *b) {
  static char bufs[4][1024];
  static int idx = 0;
  char *out = bufs[idx];
  idx = (idx + 1) & 3;
  snprintf(out, 1024, "%s%s", a, b);
  return out;
}

static const char *_pde_charat(const char *s, int i) {
  static char bufs[4][2];
  static int idx = 0;
  char *out = bufs[idx];
  idx = (idx + 1) & 3;
  out[0] = s[i];
  out[1] = '\0';
  return out;
}

// Java expand(): grow an array keeping contents; new tail zero-filled.
// Sizes are remembered from _pde_array_new so the copy length is known.
typedef struct { void *ptr; size_t elems; size_t esz; } _PdeArrMeta;
static _PdeArrMeta _pdeArrs[512];
static int _pdeArrCount = 0;

static inline void _pde_arr_register(void *p, size_t elems, size_t esz) {
  if (!p || _pdeArrCount >= 512) return;
  for (int i = 0; i < _pdeArrCount; i++) {
    if (_pdeArrs[i].ptr == p) {
      _pdeArrs[i].elems = elems;
      _pdeArrs[i].esz = esz;
      return;
    }
  }
  _pdeArrs[_pdeArrCount].ptr = p;
  _pdeArrs[_pdeArrCount].elems = elems;
  _pdeArrs[_pdeArrCount].esz = esz;
  _pdeArrCount++;
}

static inline void *_pde_expand(void **arrp, int newSize, size_t esz) {
  if (newSize < 0) newSize = 0;
  void *old = *arrp;
  size_t oldElems = 0;
  for (int i = 0; i < _pdeArrCount; i++) {
    if (_pdeArrs[i].ptr == old) { oldElems = _pdeArrs[i].elems; break; }
  }
  void *grown = realloc(old, (size_t)newSize * esz);
  if (!grown) return old;
  if ((size_t)newSize > oldElems) {
    memset((char *)grown + oldElems * esz, 0, ((size_t)newSize - oldElems) * esz);
  }
  *arrp = grown;
  _pde_arr_register(grown, (size_t)newSize, esz);
  return grown;
}

// Element count of a registered (heap) array; -1 when unknown.
static inline int _pde_arr_len(const void *ptr) {
  for (int i = 0; i < _pdeArrCount; i++) {
    if (_pdeArrs[i].ptr == ptr) return (int)_pdeArrs[i].elems;
  }
  return -1;
}

// .length rewrite target. True C arrays hit the sizeof branch (exact count);
// heap arrays allocated via _pde_array_new/expand hit the registry lookup.
// The match uses the element type (A[0]) so the association is exactly
// "pointer to array of the element type", e.g. float(*)[3] for float a[3];
// always-compatible with the actual size constant.
#define _pde_len(A) _pde_len_sel(A)
#define _pde_len_sel(A) _Generic(&(A), \
    __typeof__(A[0])(*)[(sizeof(A) / sizeof((A)[0]))]: (int)(sizeof(A) / sizeof(*(A))), \
    default: _pde_arr_len((const void *)(A)))

// java.lang.Object stand-in: an empty value class, so `new Object()` and
// `(Object) list.get(i)` compile. sizeof(Object) is 1 (deviation from Java
// identity semantics is inherent to the value-copy model).
typedef struct _PdeObject { char _pde_pad; } Object;
static inline Object Object_ctor(void) { return (Object){0}; }

// ArrayList (in-memory subset). Each element is a malloc'ed copy of the
// original value; `get` returns a void* to that copy so the Processing
// cast idiom `(Type) list.get(i)` compiles to `*(Type *) _pde_ag_get(...)`.
typedef struct _PdeArrayList {
  void **data;
  int len;
  int cap;
} _PdeArrayListT;
typedef _PdeArrayListT *ArrayList;

static inline ArrayList _pde_ag_new(void) { return calloc(sizeof(_PdeArrayListT), 1); }

static inline void *_pde_ag_dup(const void *src, size_t esz) {
  void *p = malloc(esz ? esz : 1);
  if (p && src) memcpy(p, src, esz);
  return p;
}

static inline void _pde_ag_add_fn(ArrayList l, void *elem) {
  if (!l) return;
  if (l->len >= l->cap) {
    int nc = l->cap ? l->cap * 2 : 8;
    void **nd = (void **)realloc(l->data, sizeof(void *) * (size_t)nc);
    if (!nd) return;
    l->data = nd;
    l->cap = nc;
  }
  l->data[l->len++] = elem;
}
#define _pde_ag_add(L, I)                                                        \
  _pde_ag_add_fn((L), ({ typeof(I) _pde_v = (I); _pde_ag_dup(&_pde_v, sizeof(I)); }))

static inline void *_pde_ag_get(const ArrayList l, int i) {
  if (!l || i < 0 || i >= l->len) return NULL;
  return l->data[i];
}

static inline int _pde_ag_size(const ArrayList l) { return l ? l->len : 0; }

static inline void _pde_ag_set_fn(ArrayList l, int i, void *elem) {
  if (!l || i < 0 || i >= l->len) return;
  free(l->data[i]);
  l->data[i] = elem;
}
#define _pde_ag_set(L, I, X)                                                      \
  _pde_ag_set_fn((L), (I), ({ typeof(X) _pde_v = (X); _pde_ag_dup(&_pde_v, sizeof(X)); }))

static inline void _pde_ag_remove(ArrayList l, int i) {
  if (!l || i < 0 || i >= l->len) return;
  free(l->data[i]);
  for (int k = i; k < l->len - 1; k++) l->data[k] = l->data[k + 1];
  l->len--;
}

static inline void _pde_ag_clear(ArrayList l) {
  if (!l) return;
  for (int i = 0; i < l->len; i++) free(l->data[i]);
  free(l->data);
  l->data = NULL;
  l->len = 0;
  l->cap = 0;
}

// no-arg <Type> generic terminals that write an empty list
#define _pde_ag_clear_all(Ls) do { for (int _i = 0; _i < 8; _i++) _pde_ag_clear((Ls)[_i]); } while (0)

// String[] utilities: splitTokens, split, join ///////////////////////////
// Results are registered heap arrays so .length works through _pde_len.

static inline char *_pde_dup_len(const char *s, size_t n) {
  char *out = (char *)malloc(n + 1);
  if (!out) return (char *)(n ? s : "");
  memcpy(out, s, n);
  out[n] = '\0';
  return out;
}

// splitTokens(s[, delims]): break into non-empty runs separated by any char
// in delims (defaults to whitespace).
static inline const char **splitTokens2(const char *s, const char *delims) {
  static const char *ws = " \t\n\r\f";
  if (!delims) delims = ws;
  int count = 0;
  const char *p = s;
  while (*p) {
    while (*p && strchr(delims, (unsigned char)*p)) p++;
    if (*p) { count++; while (*p && !strchr(delims, (unsigned char)*p)) p++; }
  }
  const char **out = (const char **)calloc(count > 0 ? (size_t)count : 1, sizeof(const char *));
  int idx = 0;
  p = s;
  while (*p) {
    while (*p && strchr(delims, (unsigned char)*p)) p++;
    if (*p) {
      const char *start = p;
      while (*p && !strchr(delims, (unsigned char)*p)) p++;
      out[idx++] = _pde_dup_len(start, (size_t)(p - start));
    }
  }
  _pde_arr_register(out, (size_t)(count > 0 ? count : 1), sizeof(const char *));
  return out;
}
static inline const char **splitTokens1(const char *s) { return splitTokens2(s, NULL); }
#define SPLITTOKENS_CHOOSER(_1, _2, _3, NAME, ...) NAME
#define splitTokens(...) SPLITTOKENS_CHOOSER(0, __VA_ARGS__, splitTokens2, splitTokens1)(__VA_ARGS__)

// split(s, delim): exact substring delimiter (Java-literal semantics);
// trailing empty fields are dropped like Java's split().
static inline const char **split(const char *s, const char *delim) {
  if (!delim || !*delim) {
    const char **one = (const char **)calloc(1, sizeof(const char *));
    one[0] = _pde_dup_len(s, strlen(s));
    _pde_arr_register(one, 1, sizeof(const char *));
    return one;
  }
  size_t dlen = strlen(delim);
  int fields = 1;
  const char *p = s, *hit;
  while ((hit = strstr(p, delim)) != NULL) { fields++; p = hit + dlen; }
  const char **out = (const char **)calloc((size_t)fields, sizeof(const char *));
  int idx = 0;
  int lastNonEmpty = -1;
  p = s;
  while ((hit = strstr(p, delim)) != NULL) {
    out[idx] = _pde_dup_len(p, (size_t)(hit - p));
    if (out[idx][0] != '\0') lastNonEmpty = idx;
    idx++;
    p = hit + dlen;
  }
  out[idx] = _pde_dup_len(p, strlen(p));
  if (out[idx][0] != '\0') lastNonEmpty = idx;
  int n = lastNonEmpty + 1;          // drop trailing empty fields
  if (n < 1) n = 1;                  // all empty -> single empty field
  _pde_arr_register(out, (size_t)n, sizeof(const char *));
  return out;
}

// join(arr, sep): concatenate registered String[] with sep between elements.
static inline const char *join(const char *const *arr, const char *sep) {
  static char *joinBufs[4];
  static int joinIdx = 0;
  char *out = joinBufs[joinIdx];
  joinIdx = (joinIdx + 1) & 3;
  if (out) free(out);
  int n = _pde_arr_len(arr);
  if (n < 0) n = 0;
  size_t sepLen = sep ? strlen(sep) : 0;
  size_t total = 1;
  for (int i = 0; i < n; i++) total += strlen(arr[i]) + sepLen;
  out = (char *)malloc(total);
  if (!out) return "";
  out[0] = '\0';
  for (int i = 0; i < n; i++) {
    if (i) strcat(out, sep);
    strcat(out, arr[i]);
  }
  joinBufs[(joinIdx + 3) & 3] = out;
  return out;
}

// Array utilities: append/concat/subset/shorten/reverse/splice/sort /////

#define _PDE_DEF_ARRAY_OPS(T, SFX)                                      \
  static inline T *_pde_append_##SFX(T *arr, T e) {                     \
    int n = _pde_arr_len(arr); if (n < 0) n = 0;                        \
    _pde_expand((void **)&arr, n + 1, sizeof(T));                       \
    arr[n] = e;                                                         \
    return arr;                                                         \
  }                                                                     \
  static inline T *_pde_shorten_##SFX(T *arr) {                         \
    int n = _pde_arr_len(arr); if (n <= 0) return arr;                  \
    _pde_expand((void **)&arr, n - 1, sizeof(T));                       \
    return arr;                                                         \
  }                                                                     \
  static inline T *_pde_reverse_##SFX(T *arr) {                         \
    int n = _pde_arr_len(arr); if (n < 0) n = 0;                        \
    for (int i = 0, j = n - 1; i < j; i++, j--) {                       \
      T t = arr[i]; arr[i] = arr[j]; arr[j] = t;                        \
    }                                                                   \
    return arr;                                                         \
  }                                                                     \
  static inline T *_pde_concat_##SFX(T *a, const T *b) {                \
    int na = _pde_arr_len(a), nb = _pde_arr_len(b);                     \
    if (na < 0) na = 0; if (nb < 0) nb = 0;                             \
    _pde_expand((void **)&a, na + nb, sizeof(T));                       \
    for (int i = 0; i < nb; i++) a[na + i] = b[i];                      \
    return a;                                                           \
  }                                                                     \
  static inline T *_pde_subset_##SFX(T *arr, int start, int count) {    \
    int n = _pde_arr_len(arr); if (n < 0) n = 0;                        \
    if (start < 0) start = 0;                                           \
    if (count < 0) count = n - start;                                   \
    if (count < 0) count = 0;                                           \
    if (start + count > n) count = n - start;                           \
    if (count < 0) count = 0;                                           \
    T *out = (T *)calloc((size_t)(count > 0 ? count : 1), sizeof(T));   \
    for (int i = 0; i < count; i++) out[i] = arr[start + i];            \
    _pde_arr_register(out, (size_t)(count > 0 ? count : 1), sizeof(T)); \
    return out;                                                         \
  }                                                                     \
  static inline T *_pde_splice_##SFX(T *arr, T v, int index) {          \
    int n = _pde_arr_len(arr); if (n < 0) n = 0;                        \
    if (index < 0) index = 0;                                           \
    if (index > n) index = n;                                           \
    _pde_expand((void **)&arr, n + 1, sizeof(T));                       \
    for (int i = n; i > index; i--) arr[i] = arr[i - 1];                \
    arr[index] = v;                                                     \
    return arr;                                                         \
  }

_PDE_DEF_ARRAY_OPS(float, float)
_PDE_DEF_ARRAY_OPS(int, int)
_PDE_DEF_ARRAY_OPS(bool, bool)
_PDE_DEF_ARRAY_OPS(uint32_t, color)
_PDE_DEF_ARRAY_OPS(double, double)
_PDE_DEF_ARRAY_OPS(const char *, string)

static inline int _pde_cmp_int(const void *a, const void *b) {
  return (*(const int *)a > *(const int *)b) - (*(const int *)a < *(const int *)b);
}
static inline int _pde_cmp_float(const void *a, const void *b) {
  float x = *(const float *)a, y = *(const float *)b;
  return (x > y) - (x < y);
}
static inline int _pde_cmp_double(const void *a, const void *b) {
  double x = *(const double *)a, y = *(const double *)b;
  return (x > y) - (x < y);
}
static inline int _pde_cmp_color(const void *a, const void *b) {
  return (*(const uint32_t *)a > *(const uint32_t *)b) - (*(const uint32_t *)a < *(const uint32_t *)b);
}
static inline int _pde_cmp_str(const void *a, const void *b) {
  return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static inline void _pde_sort_int(int *arr, int n) { qsort(arr, (size_t)n, sizeof(int), _pde_cmp_int); }
static inline void _pde_sort_float(float *arr, int n) { qsort(arr, (size_t)n, sizeof(float), _pde_cmp_float); }
static inline void _pde_sort_double(double *arr, int n) { qsort(arr, (size_t)n, sizeof(double), _pde_cmp_double); }
static inline void _pde_sort_color(uint32_t *arr, int n) { qsort(arr, (size_t)n, sizeof(uint32_t), _pde_cmp_color); }
static inline void _pde_sort_string(const char **arr, int n) { qsort(arr, (size_t)n, sizeof(const char *), _pde_cmp_str); }

#define append(arr, e) _Generic((arr),                                   \
    float *: _pde_append_float, int *: _pde_append_int,                  \
    bool *: _pde_append_bool, uint32_t *: _pde_append_color,             \
    double *: _pde_append_double, const char **: _pde_append_string,     \
    default: _pde_append_float)(arr, e)

#define shorten(arr) _Generic((arr),                                     \
    float *: _pde_shorten_float, int *: _pde_shorten_int,                \
    bool *: _pde_shorten_bool, uint32_t *: _pde_shorten_color,           \
    double *: _pde_shorten_double, const char **: _pde_shorten_string,   \
    default: _pde_shorten_float)(arr)

#define reverse(arr) _Generic((arr),                                     \
    float *: _pde_reverse_float, int *: _pde_reverse_int,                \
    bool *: _pde_reverse_bool, uint32_t *: _pde_reverse_color,           \
    double *: _pde_reverse_double, const char **: _pde_reverse_string,   \
    default: _pde_reverse_float)(arr)

#define concat(a, b) _Generic((a),                                        \
    float *: _pde_concat_float, int *: _pde_concat_int,                   \
    bool *: _pde_concat_bool, uint32_t *: _pde_concat_color,              \
    double *: _pde_concat_double, const char **: _pde_concat_string,      \
    default: _pde_concat_float)(a, b)

#define _SUBSET_CHOOSER(_1, _2, _3, NAME, ...) NAME
#define subset(...) _SUBSET_CHOOSER(__VA_ARGS__, _pde_dispatch_subset3, _pde_dispatch_subset2, DUMMY)(__VA_ARGS__)
#define _pde_dispatch_subset2(arr, start) _Generic((arr),                 \
    float *: _pde_subset_float, int *: _pde_subset_int,                   \
    bool *: _pde_subset_bool, uint32_t *: _pde_subset_color,              \
    double *: _pde_subset_double, const char **: _pde_subset_string,      \
    default: _pde_subset_float)((arr), (start), -1)
#define _pde_dispatch_subset3(arr, start, count) _Generic((arr),          \
    float *: _pde_subset_float, int *: _pde_subset_int,                   \
    bool *: _pde_subset_bool, uint32_t *: _pde_subset_color,              \
    double *: _pde_subset_double, const char **: _pde_subset_string,      \
    default: _pde_subset_float)((arr), (start), (count))

#define _SPLICE_CHOOSER(_1, _2, _3, NAME, ...) NAME
#define splice(...) _SPLICE_CHOOSER(__VA_ARGS__, _pde_dispatch_splice3, _pde_dispatch_splice2, DUMMY)(__VA_ARGS__)
#define _pde_dispatch_splice2(arr, value) _Generic((arr),                 \
    float *: _pde_splice_float, int *: _pde_splice_int,                   \
    bool *: _pde_splice_bool, uint32_t *: _pde_splice_color,              \
    double *: _pde_splice_double, const char **: _pde_splice_string,      \
    default: _pde_splice_float)((arr), (value), -1)
#define _pde_dispatch_splice3(arr, value, index) _Generic((arr),          \
    float *: _pde_splice_float, int *: _pde_splice_int,                   \
    bool *: _pde_splice_bool, uint32_t *: _pde_splice_color,              \
    double *: _pde_splice_double, const char **: _pde_splice_string,      \
    default: _pde_splice_float)((arr), (value), (index))

#define SORT_CHOOSER(_1, _2, NAME, ...) NAME
#define sort(...) SORT_CHOOSER(__VA_ARGS__, _pde_dispatch_sort3, _pde_dispatch_sort2, DUMMY)(__VA_ARGS__)
#define _pde_dispatch_sort2(arr) _pde_dispatch_sort3((arr), _pde_len(arr))
#define _pde_dispatch_sort3(arr, count) _Generic((arr),                   \
    float *: _pde_sort_float, int *: _pde_sort_int,                       \
    bool *: _pde_sort_float, uint32_t *: _pde_sort_color,                 \
    double *: _pde_sort_double, const char **: _pde_sort_string,          \
    default: _pde_sort_float)((arr), (count))

// pixels ////////////////////////////////////////////

static inline void loadPixels(void) {
  // read back from the offscreen canvas (screen readback is unreliable on
  // some GL drivers); RT textures are stored bottom-up, flip while copying
  rlDrawRenderBatchActive(); // submit queued draws so they reach the texture
  Image img = LoadImageFromTexture(_canvas.texture);
  Color *colors = LoadImageColors(img);
  int count = width * height;
  for (int y = 0; y < height; y++) {
    Color *srcRow = &colors[(height - 1 - y) * width];
    Color *dstRow = &pixels[y * width];
    for (int x = 0; x < width; x++) dstRow[x] = srcRow[x];
  }
  UnloadImageColors(colors);
  UnloadImage(img);
}

static inline void updatePixels(void) {
  UpdateTexture(_pixelsTexture, pixels);
  DrawTexture(_pixelsTexture, 0, 0, WHITE);
}

static inline PGraphics createGraphics(int w, int h) {
  PGraphics pg = LoadRenderTexture(w, h);
  SetTextureFilter(pg.texture, TEXTURE_FILTER_POINT);
  return pg;
}

static inline void beginGraphics(PGraphics pg) {
  BeginTextureMode(pg);
}

static inline void endGraphics(void) {
  EndTextureMode();
}

// image() dispatch: PGraphics canvas vs disk-loaded PImage ////////////

// single 5-param form each; w/h <= 0 means "draw at natural size".
// pde2c rewrites 3-arg sketch calls to pass 0, 0 for the size.
static inline void image_pg(PGraphics pg, float x, float y, float w, float h) {
  if (w <= 0.0f || h <= 0.0f) {
    // canvas textures render bottom-up -> flip
    DrawTextureRec(pg.texture, (Rectangle){ 0, 0, (float)pg.texture.width, (float)-pg.texture.height }, (Vector2){ x, y }, WHITE);
  } else {
    DrawTexturePro(pg.texture,
                   (Rectangle){ 0, 0, (float)pg.texture.width, (float)-pg.texture.height },
                   (Rectangle){ x, y, w, h }, (Vector2){ 0, 0 }, 0.0f, WHITE);
  }
}

// PImage — thin alias over raylib Image; width/height come free.
typedef Image PImage;

// tint affects image draws only (Processing model)
static Color _tintColor = { 255, 255, 255, 255 };
static bool _tintEnabled = false;

static inline void tint4(float r, float g, float b, float a) {
  _tintColor = (Color){ (unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a };
  _tintEnabled = true;
}
static inline void tint3(float r, float g, float b) { tint4(r, g, b, 255.0f); }
static inline void tint2(float grayOrColor, float alpha) {
  if (grayOrColor > 255.0f) {
    uint32_t c = (uint32_t)grayOrColor;
    tint4((float)(c & 0xFF), (float)((c >> 8) & 0xFF), (float)((c >> 16) & 0xFF), alpha);
  } else {
    tint4(grayOrColor, grayOrColor, grayOrColor, alpha);
  }
}
static inline void tint1(uint32_t c) {
  if (c <= 255) tint4((float)c, (float)c, (float)c, 255);
  else tint4((float)(c & 0xFF), (float)((c >> 8) & 0xFF), (float)((c >> 16) & 0xFF), 255.0f);
}
#define TINT_CHOOSER(_1, _2, _3, _4, NAME, ...) NAME
#define tint(...) TINT_CHOOSER(__VA_ARGS__, tint4, tint3, tint2, tint1)(__VA_ARGS__)
static inline void noTint(void) { _tintEnabled = false; }

// small GPU texture cache so per-frame image() redraws don't re-upload
// the whole CPU buffer; keyed by img.data pointer, dropped on mutation
typedef struct { void *key; Texture2D tex; } _PImageTexEntry;
static _PImageTexEntry _pimageTexCache[16];
static int _pimageTexCacheN = 0;

static Texture2D _pimage_texture(const PImage *img) {
  for (int i = 0; i < _pimageTexCacheN; i++)
    if (_pimageTexCache[i].key == img->data) return _pimageTexCache[i].tex;
  Texture2D t = LoadTextureFromImage(*img);
  if (_pimageTexCacheN < 16) {
    _pimageTexCache[_pimageTexCacheN++] = (_PImageTexEntry){ img->data, t };
  }
  return t;
}

static void _pimage_invalidate(const void *dataKey) {
  for (int i = 0; i < _pimageTexCacheN; i++) {
    if (_pimageTexCache[i].key == dataKey) {
      UnloadTexture(_pimageTexCache[i].tex);
      _pimageTexCache[i] = _pimageTexCache[--_pimageTexCacheN];
      return;
    }
  }
}

// Processing filter constants, prefixed: raylib's color macros (#define GRAY
// ...) would collide with the bare Processing names; pde2c maps them over
enum { _PIMAGE_BLUR = 101, _PIMAGE_GRAY = 102, _PIMAGE_INVERT = 103,
       _PIMAGE_THRESHOLD = 104, _PIMAGE_POSTERIZE = 105, _PIMAGE_OPAQUE = 106 };

static inline PImage loadImage(const char *path) {
  PImage img = LoadImage(path);
  if (img.data == NULL) {
    fprintf(stderr, "processing.h: loadImage(\"%s\") failed, using 1x1 blank\n", path);
    img = GenImageColor(1, 1, BLANK);
    return img;
  }
  ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8); // stable pixel layout
  return img;
}

static inline PImage createImage(int w, int h, int format) {
  (void)format; // RGB/ARGB/etc. all normalize to RGBA8
  return GenImageColor(w, h, BLANK);
}

static inline PImage pimage_new(int w, int h) { return GenImageColor(w, h, BLANK); }

static inline void image_pim(PImage img, float x, float y, float w, float h) {
  if (w <= 0.0f || h <= 0.0f) { w = img.width; h = img.height; }
  // CPU-loaded images are top-down like Processing's coordinate system
  DrawTexturePro(_pimage_texture(&img),
                 (Rectangle){ 0, 0, (float)img.width, (float)img.height },
                 (Rectangle){ x, y, w, h }, (Vector2){ 0, 0 }, 0.0f,
                 _tintEnabled ? _tintColor : WHITE);
}

#define image(first, ...) \
  _Generic((first), Image: image_pim, RenderTexture2D: image_pg)(first, __VA_ARGS__)

// ---- PImage member operations (rewritten by pde2c from img.op(...) form)

// pixels[] aliases the RGBA8 buffer directly: uint32 little-endian view of
// R8G8B8A8 bytes equals the pack_color ABGR layout used everywhere else
static inline uint32_t *pimage_loadPixels(PImage *img) {
  return (uint32_t *)img->data;
}
static inline uint32_t *pimage_pixels(PImage *img) {
  return (uint32_t *)img->data;
}
static inline void pimage_updatePixels(PImage *img) {
  _pimage_invalidate(img->data); // redraw re-uploads mutated pixels
}

static inline void pimage_resize(PImage *img, int w, int h) {
  _pimage_invalidate(img->data);
  ImageResize(img, w, h);
  ImageFormat(img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
}

static void pimage_filter(PImage *img, int kind, float arg) {
  if (img->format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 || img->data == NULL) return;
  _pimage_invalidate(img->data);
  int n = img->width * img->height;
  uint8_t *px = (uint8_t *)img->data;
  switch (kind) {
    case _PIMAGE_GRAY:
      for (int i = 0; i < n; i++) {
        uint8_t g = (uint8_t)((px[i*4] * 77 + px[i*4+1] * 151 + px[i*4+2] * 28) >> 8);
        px[i*4] = px[i*4+1] = px[i*4+2] = g;
      }
      break;
    case _PIMAGE_INVERT:
      for (int i = 0; i < n * 4; i += 4) {
        px[i] = 255 - px[i]; px[i+1] = 255 - px[i+1]; px[i+2] = 255 - px[i+2];
      }
      break;
    case _PIMAGE_THRESHOLD: {
      float t = (arg <= 0.0f || arg > 1.0f) ? 0.5f : arg;
      for (int i = 0; i < n; i++) {
        uint8_t g = (uint8_t)((px[i*4] * 77 + px[i*4+1] * 151 + px[i*4+2] * 28) >> 8);
        uint8_t v = (g >= (uint8_t)(t * 255.0f)) ? 255 : 0;
        px[i*4] = px[i*4+1] = px[i*4+2] = v;
      }
      break;
    }
    case _PIMAGE_POSTERIZE: {
      int levels = (arg < 2.0f) ? 2 : (arg > 255.0f ? 255 : (int)arg);
      for (int i = 0; i < n * 4; i += 4)
        for (int c = 0; c < 3; c++)
          px[i+c] = (uint8_t)((px[i+c] >> levels << levels) | (1 << (levels - 1)));
      break;
    }
    case _PIMAGE_OPAQUE:
      for (int i = 0; i < n; i++) px[i*4+3] = 255;
      break;
    case _PIMAGE_BLUR: {
      // box blur, radius from arg (default 1), three passes approximate gaussian
      int r = (arg < 1.0f) ? 1 : (int)arg;
      uint8_t *tmp = (uint8_t *)MemAlloc((size_t)n * 4);
      for (int pass = 0; pass < 3; pass++) {
        memcpy(tmp, px, (size_t)n * 4);
        for (int y = 0; y < img->height; y++)
          for (int x = 0; x < img->width; x++)
            for (int c = 0; c < 4; c++) {
              int sum = 0, cnt = 0;
              for (int dy = -r; dy <= r; dy++)
                for (int dx = -r; dx <= r; dx++) {
                  int sx = x + dx, sy = y + dy;
                  if (sx < 0 || sy < 0 || sx >= img->width || sy >= img->height) continue;
                  sum += tmp[(sy * img->width + sx) * 4 + c];
                  cnt++;
                }
              px[(y * img->width + x) * 4 + c] = (uint8_t)(sum / cnt);
            }
      }
      MemFree(tmp);
      break;
    }
    default:
      fprintf(stderr, "processing.h: pimage_filter(%d) not implemented\n", kind);
      break;
  }
}

static inline void pimage_mask(PImage *img, PImage *src) {
  if (img->width != src->width || img->height != src->height ||
      img->format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 ||
      src->format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) {
    fprintf(stderr, "processing.h: mask() needs same-size images\n");
    return;
  }
  uint8_t *d = (uint8_t *)img->data, *s = (uint8_t *)src->data;
  int n = img->width * img->height;
  for (int i = 0; i < n; i++)
    d[i*4+3] = (uint8_t)((d[i*4+3] * s[i*4]) >> 8); // red channel drives alpha
  _pimage_invalidate(img->data);
}

static inline void pimage_save(PImage *img, const char *path) {
  ExportImage(*img, path);
}

// PImage get(): single-pixel color (uint32 packed ABGR, same as color()/pixels)
static inline uint32_t pimage_get_px(PImage *img, int x, int y) {
  if (!img->data || x < 0 || y < 0 || x >= img->width || y >= img->height) {
    fprintf(stderr, "processing.h: get(%d,%d) out of bounds on %dx%d\n",
            x, y, img->width, img->height);
    return 0xFF000000u; // opaque black
  }
  uint8_t *px = (uint8_t *)img->data;
  px = px + ((size_t)y * img->width + (size_t)x) * 4;
  return ((uint32_t)px[3] << 24) | ((uint32_t)px[2] << 16) |
         ((uint32_t)px[1] << 8) | (uint32_t)px[0];
}

// get() -> copy of the whole image (Processing returns a PImage)
static inline PImage pimage_get_copy(PImage *img) {
  Image c = ImageCopy(*img);
  ImageFormat(&c, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
  return c;
}

// get(x, y, w, h) -> rectangular copy
static inline PImage pimage_get_region(PImage *img, int x, int y, int w, int h) {
  if (w <= 0) w = img->width - x;
  if (h <= 0) h = img->height - y;
  int rx = x < 0 ? 0 : x;
  int ry = y < 0 ? 0 : y;
  int rw = (rx + w > img->width) ? img->width - rx : w;
  int rh = (ry + h > img->height) ? img->height - ry : h;
  if (rw < 1) rw = 1;
  if (rh < 1) rh = 1;
  Image sub = GenImageColor(rw, rh, BLANK);
  uint8_t *src = (uint8_t *)img->data;
  uint8_t *dst = (uint8_t *)sub.data;
  for (int yy = 0; yy < rh; yy++)
    for (int xx = 0; xx < rw; xx++) {
      uint8_t *s = src + (((size_t)(ry + yy) * img->width + (size_t)(rx + xx)) * 4);
      uint8_t *d = dst + (((size_t)yy * rw + (size_t)xx) * 4);
      d[0]=s[0]; d[1]=s[1]; d[2]=s[2]; d[3]=s[3];
    }
  return sub;
}

// transformations (basic) /////////////////////////////////////////////////////

static inline void pushMatrix(void) { rlPushMatrix(); }
static inline void popMatrix(void) { rlPopMatrix(); }
#define push() pushMatrix()
#define pop() popMatrix()
#define ortho(...) rlOrtho(-800.0, 800.0, -450.0, 450.0, 0.1, 1000.0)
static inline void translate2(float x, float y) { rlTranslatef(x, y, 0.0f); }
static inline void translate3(float x, float y, float z) { rlTranslatef(x, y, z); }
#define TRANSLATE_CHOOSER(_1, _2, _3, NAME, ...) NAME
#define translate(...) TRANSLATE_CHOOSER(__VA_ARGS__, translate3, translate2, DUMMY)(__VA_ARGS__)
static inline void rotate(float radians) { rlRotatef(radians * 57.295779513f, 0.0f, 0.0f, 1.0f); }
static inline void scale(float s) { rlScalef(s, s, 1.0f); }

// fill ////////////////////////////

static inline void fill4(float r, float g, float b, float a) {
  _useFill = true;
  _fillColor = (Color){ (unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a };
}

static inline void fill3(float r, float g, float b) {
  fill4(r, g, b, 255.0f);
}

static inline void fill2(float grayOrColor, float alpha) {
  if (grayOrColor > 255.0f) { // packed ABGR + alpha (Processing: fill(rgb, a))
    uint32_t c = (uint32_t)grayOrColor;
    fill4((float)(c & 0xFF), (float)((c >> 8) & 0xFF), (float)((c >> 16) & 0xFF), alpha);
  } else {
    fill4(grayOrColor, grayOrColor, grayOrColor, alpha);
  }
}

static inline void fill1(uint32_t c) {
  if (c <= 255) {
    fill4(c, c, c, 255);
  } else {
    uint8_t r = c         & 0xFF;
    uint8_t g = (c >> 8)  & 0xFF;
    uint8_t b = (c >> 16) & 0xFF;
    fill4(r, g, b, 255);
  }
}

static inline void noFill(void) { _useFill = false; }

// stroke /////////////////////////////////

static inline void stroke4(float r, float g, float b, float a) {
  _useStroke = true;
  _strokeColor = (Color){ (unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a };
}

static inline void stroke3(float r, float g, float b) {
  stroke4(r, g, b, 255.0f);
}

static inline void stroke2(float grayOrColor, float alpha) {
  if (grayOrColor > 255.0f) { // packed ABGR + alpha (Processing: stroke(rgb, a))
    uint32_t c = (uint32_t)grayOrColor;
    stroke4((float)(c & 0xFF), (float)((c >> 8) & 0xFF), (float)((c >> 16) & 0xFF), alpha);
  } else {
    stroke4(grayOrColor, grayOrColor, grayOrColor, alpha);
  }
}

static inline void stroke1(uint32_t c) {
  if (c <= 255) {
    stroke4(c, c, c, 255);
  } else {
    uint8_t r = c         & 0xFF;
    uint8_t g = (c >> 8)  & 0xFF;
    uint8_t b = (c >> 16) & 0xFF;
    stroke4(r, g, b, 255);
  }
}

static inline void noStroke(void) { _useStroke = false; }

static inline void strokeWeight(float weight) {
  _strokeW = weight;
}

static inline void strokeCap(int cap) {
  _strokeCap = cap;
}

static inline void strokeJoin(int join) {
  _strokeJoin = join;
}


// line //////////////////
static inline void line6(float x1, float y1, float z1, float x2, float y2, float z2) {
  if (_useStroke) {
    if (_strokeW <= 1.0f) {
      DrawLine3D((Vector3){ x1, y1, z1 }, (Vector3){ x2, y2, z2 }, _strokeColor);
    } else {
      DrawLineEx((Vector2){ x1, y1 }, (Vector2){ x2, y2 }, _strokeW, _strokeColor);
    }
  }
}

static inline void line4(float x1, float y1, float x2, float y2) {
  line6(x1, y1, 0.0f, x2, y2, 0.0f);
}

// shapes //////////////////////////////////////////////////////////////////////////////

static inline void rectMode(int mode) { _rectModeState = mode; }
static inline void ellipseMode(int mode) { _ellipseModeState = mode; }

// normalize any rect-mode input to top-left corner + width/height
static inline void _normRect(float x, float y, float w, float h, int mode,
                             float *ox, float *oy, float *ow, float *oh) {
  switch (mode) {
    case CORNERS:
      *ox = (x < w) ? x : w;
      *oy = (y < h) ? y : h;
      *ow = fabsf(w - x);
      *oh = fabsf(h - y);
      break;
    case CENTER:
      *ox = x - w * 0.5f;
      *oy = y - h * 0.5f;
      *ow = w;
      *oh = h;
      break;
    case RADIUS:
      *ox = x - w;
      *oy = y - h;
      *ow = w * 2.0f;
      *oh = h * 2.0f;
      break;
    case CORNER:
    default:
      *ox = x;
      *oy = y;
      *ow = w;
      *oh = h;
      break;
  }
}

static inline void _strokeRectOutline(float x, float y, float w, float h) {
  if (_strokeW <= 1.0f) {
    DrawRectangleLines((int)x, (int)y, (int)(w + 0.5f), (int)(h + 0.5f), _strokeColor);
  } else {
    Vector2 topL = { x, y }; Vector2 topR = { x + w, y };
    Vector2 botR = { x + w, y + h }; Vector2 botL = { x, y + h };
    DrawLineEx(topL, topR, _strokeW, _strokeColor);
    DrawLineEx(topR, botR, _strokeW, _strokeColor);
    DrawLineEx(botR, botL, _strokeW, _strokeColor);
    DrawLineEx(botL, topL, _strokeW, _strokeColor);
  }
}

static inline void rect(float x, float y, float w, float h) {
  float rx, ry, rw, rh;
  _normRect(x, y, w, h, _rectModeState, &rx, &ry, &rw, &rh);
  if (_useFill) {
    DrawRectangleRec((Rectangle){ rx, ry, rw, rh }, _fillColor);
  }
  if (_useStroke) {
    _strokeRectOutline(rx, ry, rw, rh);
  }
}

static inline void ellipse(float x, float y, float w, float h) {
  float ex, ey, ew, eh;
  _normRect(x, y, w, h, _ellipseModeState, &ex, &ey, &ew, &eh);
  float cx = ex + ew / 2.0f;
  float cy = ey + eh / 2.0f;
  float rX = ew / 2.0f;
  float rY = eh / 2.0f;
  if (rX <= 0.0f || rY <= 0.0f) return;

  if (_useFill) {
    DrawEllipse((int)cx, (int)cy, rX, rY, _fillColor);
  }
  if (_useStroke) {
    if (_strokeW <= 1.0f) {
      DrawEllipseLines((int)cx, (int)cy, rX, rY, _strokeColor);
    } else {
      if (rX == rY) {
        DrawRing((Vector2){cx, cy}, rX - (_strokeW/2), rX + (_strokeW/2), 0, 360, 36, _strokeColor);
      } else {
        DrawEllipseLines((int)cx, (int)cy, rX, rY, _strokeColor);
      }
    }
  }
}

static inline void circle(float x, float y, float d) {
  // Processing circle() is always center + diameter
  if (d <= 0.0f) return;
  ellipse(x - d / 2.0f, y - d / 2.0f, d, d);
}

static inline void square(float x, float y, float s) {
  rect(x, y, s, s);
}

static inline void point(float x, float y) {
  if (!_useStroke) return;
  if (_strokeW <= 1.0f) {
    DrawPixelV((Vector2){ x, y }, _strokeColor);
  } else {
    DrawCircleV((Vector2){ x, y }, _strokeW * 0.5f, _strokeColor);
  }
}

static inline float lerp(float start, float stop, float amt) {
  return start + amt * (stop - start);
}

static inline float map(float value, float start1, float stop1, float start2, float stop2) {
  return start2 + (stop2 - start2) * ((value - start1) / (stop1 - start1));
}

static inline void triangle(float x1, float y1, float x2, float y2, float x3, float y3) {
  Vector2 v1 = { x1, y1 }, v2 = { x2, y2 }, v3 = { x3, y3 };
  if (_useFill) {
    DrawTriangle(v1, v2, v3, _fillColor);
  }
  if (_useStroke) {
    DrawLineEx(v1, v2, _strokeW, _strokeColor);
    DrawLineEx(v2, v3, _strokeW, _strokeColor);
    DrawLineEx(v3, v1, _strokeW, _strokeColor);
  }
}

static inline void quad(float x1, float y1, float x2, float y2,
                        float x3, float y3, float x4, float y4) {
  Vector2 v1 = { x1, y1 }, v2 = { x2, y2 }, v3 = { x3, y3 }, v4 = { x4, y4 };
  if (_useFill) {
    DrawTriangle(v1, v2, v3, _fillColor);
    DrawTriangle(v1, v3, v4, _fillColor);
  }
  if (_useStroke) {
    DrawLineEx(v1, v2, _strokeW, _strokeColor);
    DrawLineEx(v2, v3, _strokeW, _strokeColor);
    DrawLineEx(v3, v4, _strokeW, _strokeColor);
    DrawLineEx(v4, v1, _strokeW, _strokeColor);
  }
}

static inline void arc(float x, float y, float w, float h, float start, float stop) {
  float ex, ey, ew, eh;
  _normRect(x, y, w, h, _ellipseModeState, &ex, &ey, &ew, &eh);
  float cx = ex + ew / 2.0f;
  float cy = ey + eh / 2.0f;
  float rX = ew / 2.0f;
  float rY = eh / 2.0f;
  if (rX <= 0.0f || rY <= 0.0f) return;

  float a0 = start, a1 = stop;
  while (a1 < a0) a1 += TWO_PI;

  // sampled arc: independent of raylib's DrawCircleSector quirks,
  // handles ellipses without matrix scaling, any winding
  const int segments = 48;
  Vector2 prev = { cx + cosf(a0) * rX, cy + sinf(a0) * rY };
  Vector2 first = prev;

  for (int i = 1; i <= segments; i++) {
    float t = a0 + (a1 - a0) * (float)i / (float)segments;
    Vector2 cur = { cx + cosf(t) * rX, cy + sinf(t) * rY };

    if (_useFill && i > 1) {
      // backface culling is disabled at init -> winding-safe
      DrawTriangle((Vector2){ cx, cy }, prev, cur, _fillColor);
    }
    if (_useStroke) {
      DrawLineEx(prev, cur, _strokeW, _strokeColor);
      // pie edges from center at both arc ends
      if (i == 1) DrawLineEx((Vector2){ cx, cy }, prev, _strokeW, _strokeColor);
      if (i == segments) DrawLineEx((Vector2){ cx, cy }, cur, _strokeW, _strokeColor);
    }
    prev = cur;
  }
}

static inline void bezier(float x1, float y1, float x2, float y2,
                          float x3, float y3, float x4, float y4) {
  if (!_useStroke) return;
  const int steps = 32;
  float px = x1, py = y1;
  for (int i = 1; i <= steps; i++) {
    float t = (float)i / (float)steps;
    float mt = 1.0f - t;
    float bx = mt*mt*mt*x1 + 3.0f*mt*mt*t*x2 + 3.0f*mt*t*t*x3 + t*t*t*x4;
    float by = mt*mt*mt*y1 + 3.0f*mt*mt*t*y2 + 3.0f*mt*t*t*y3 + t*t*t*y4;
    DrawLineEx((Vector2){ px, py }, (Vector2){ bx, by }, _strokeW, _strokeColor);
    px = bx; py = by;
  }
}

// bezierPoint / bezierTangent: pure cubic-bezier evaluation (Processing API)
static inline float bezierPoint(float a, float b, float c, float d, float t) {
  float mt = 1.0f - t;
  return mt*mt*mt*a + 3.0f*mt*mt*t*b + 3.0f*mt*t*t*c + t*t*t*d;
}
static inline float bezierTangent(float a, float b, float c, float d, float t) {
  return (3.0f*t*t*(-a+3.0f*b-3.0f*c+d) +
          6.0f*t*(a-2.0f*b+c) + 3.0f*(b-a));
}

// Catmull-Rom spline (curvePoint / curveTangent / curveTightness / curve()).
// curveTightness is accepted for compatibility but the curve basis keeps the
// default 0 tightness; curvePoint uses Processing's canonical formula.
static float _curveTightness = 0.0f;
static inline void curveTightness(float s) { _curveTightness = s; }

static inline float curvePoint(float a, float b, float c, float d, float t) {
  float tt = t * t;
  float ttt = t * tt;
  return 0.5f * ((2.0f * b) + (-a + c) * t +
                 (2.0f * a - 5.0f * b + 4.0f * c - d) * tt +
                 (-a + 3.0f * b - 3.0f * c + d) * ttt);
}

static inline float curveTangent(float a, float b, float c, float d, float t) {
  float tt = t * t;
  float s1 = (2.0f * a - 5.0f * b + 4.0f * c - d);
  float s2 = (-a + 3.0f * b - 3.0f * c + d);
  return 0.5f * ((-a + c) + 2.0f * s1 * t + 3.0f * s2 * tt);
}

// curve(x1,y1, x2,y2, x3,y3, x4,y4): draws the Catmull-Rom segment between
// points 2 and 3 (points 1 and 4 are the enclosing control points), sampled
// into polyline segments the same way bezier() is.
static inline void curve(float x1, float y1, float x2, float y2,
                        float x3, float y3, float x4, float y4) {
  if (!_useStroke) return;
  const int steps = 32;
  float px = x2, py = y2;
  for (int i = 1; i <= steps; i++) {
    float t = (float)i / (float)steps;
    float cx = curvePoint(x1, x2, x3, x4, t);
    float cy = curvePoint(y1, y2, y3, y4, t);
    DrawLineEx((Vector2){ px, py }, (Vector2){ cx, cy }, _strokeW, _strokeColor);
    px = cx; py = cy;
  }
}

// polygon engine (beginShape / vertex / endShape) //////////////////////////////////////

static inline void beginShape1(int mode) {
  _shapeMode = (mode >= POINTS && mode <= POLYGON) ? mode : POLYGON;
  _shapeVertCount = 0;
  _shapeModeClose = 0;
}

static inline void beginShape0(void) {
  beginShape1(OPEN);
}

static inline void vertex2(float x, float y) {
  if (_shapeVertCount < 0 || _shapeVertCount >= _SHAPE_MAX_VERTS) return;
  _shapeVerts[_shapeVertCount].x = x;
  _shapeVerts[_shapeVertCount].y = y;
  _shapeVertCount++;
}

static inline void vertex3(float x, float y, float z) {
  (void)z; // 2D projection for now
  vertex2(x, y);
}

// cubic bezier segment appended to the current shape (Processing bezierVertex)
static inline void bezierVertex(float cx1, float cy1, float cx2, float cy2,
                                float x, float y) {
  if (_shapeVertCount < 1 || _shapeVertCount + 16 >= _SHAPE_MAX_VERTS) return;
  Vector2 p0 = _shapeVerts[_shapeVertCount - 1];
  Vector2 p1 = { cx1, cy1 };
  Vector2 p2 = { cx2, cy2 };
  Vector2 p3 = { x, y };
  const int steps = 16;
  for (int i = 1; i <= steps; i++) {
    float t = (float)i / (float)steps;
    float u = 1.0f - t;
    vertex2(u*u*u*p0.x + 3*u*u*t*p1.x + 3*u*t*t*p2.x + t*t*t*p3.x,
            u*u*u*p0.y + 3*u*u*t*p1.y + 3*u*t*t*p2.y + t*t*t*p3.y);
  }
}

// 8-arg form: explicit start anchor (x0,y0) replaces the implicit previous vertex
static inline void bezierVertex8(float x0, float y0, float cx1, float cy1,
                                 float cx2, float cy2, float x, float y) {
  if (_shapeVertCount < 1 || _shapeVertCount + 16 >= _SHAPE_MAX_VERTS) return;
  _shapeVerts[_shapeVertCount - 1].x = x0;
  _shapeVerts[_shapeVertCount - 1].y = y0;
  bezierVertex(cx1, cy1, cx2, cy2, x, y);
}

static inline float _polyCross(Vector2 o, Vector2 a, Vector2 b) {
  return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

static inline bool _pointInTri(Vector2 p, Vector2 a, Vector2 b, Vector2 c) {
  float d1 = _polyCross(a, b, p);
  float d2 = _polyCross(b, c, p);
  float d3 = _polyCross(c, a, p);
  bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
  bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
  return !(hasNeg && hasPos);
}

static void _endShapeDraw(void) {
  int n = _shapeVertCount;
  if (n < 2) { _shapeVertCount = -1; return; }
  int mode = _shapeMode;

  // vertex modes wider than POLYGON: draw points/lines/triangles/quads
  // directly (stroke = edges when both set, else fill).
  if (mode == POINTS) {
    for (int i = 0; i < n; i++) {
      float r = _strokeW > 0.0f ? _strokeW : 1.0f;
      if (_useFill) DrawCircleV(_shapeVerts[i], r, _fillColor);
      else if (_useStroke) DrawCircleV(_shapeVerts[i], r, _strokeColor);
    }
    _shapeVertCount = -1;
    return;
  }
  if (mode == LINES) {
    for (int i = 0; i + 1 < n; i += 2)
      DrawLineEx(_shapeVerts[i], _shapeVerts[i + 1], _strokeW, _strokeColor);
    _shapeVertCount = -1;
    return;
  }
  if (mode == TRIANGLES || mode == TRIANGLE_STRIP || mode == TRIANGLE_FAN) {
    for (int i = 0; i + 2 < n; i++) {
      if (mode == TRIANGLES && (i % 3) != 0) continue;
      if (mode == TRIANGLE_FAN && i > 0 && i + 2 < n) {
        Vector2 a = _shapeVerts[0], b = _shapeVerts[i], c = _shapeVerts[i + 1];
        if (_useFill) DrawTriangle(a, b, c, _fillColor);
        if (_useStroke) {
          DrawLineEx(a, b, _strokeW, _strokeColor);
          DrawLineEx(b, c, _strokeW, _strokeColor);
        }
        continue;
      }
      Vector2 a = _shapeVerts[i], b = _shapeVerts[i + 1], c = _shapeVerts[i + 2];
      if (_useFill) DrawTriangle(a, b, c, _fillColor);
      if (_useStroke) {
        DrawLineEx(a, b, _strokeW, _strokeColor);
        DrawLineEx(b, c, _strokeW, _strokeColor);
        DrawLineEx(c, a, _strokeW, _strokeColor);
      }
    }
    _shapeVertCount = -1;
    return;
  }
  if (mode == QUADS || mode == QUAD_STRIP) {
    int step = (mode == QUADS) ? 4 : 2;
    for (int i = 0; i + 3 < n; i += step) {
      Vector2 a = _shapeVerts[i], b = _shapeVerts[i + 1];
      Vector2 c = _shapeVerts[i + 3], d = _shapeVerts[i + 2];
      if (_useFill) {
        DrawTriangle(a, b, c, _fillColor);
        DrawTriangle(a, c, d, _fillColor);
      }
      if (_useStroke) {
        DrawLineEx(a, b, _strokeW, _strokeColor);
        DrawLineEx(b, d, _strokeW, _strokeColor);
        DrawLineEx(d, c, _strokeW, _strokeColor);
        DrawLineEx(c, a, _strokeW, _strokeColor);
      }
    }
    _shapeVertCount = -1;
    return;
  }

  if (_useStroke) {
    for (int i = 0; i < n - 1; i++) {
      DrawLineEx(_shapeVerts[i], _shapeVerts[i + 1], _strokeW, _strokeColor);
    }
    if (_shapeModeClose == CLOSE && n > 2) {
      DrawLineEx(_shapeVerts[n - 1], _shapeVerts[0], _strokeW, _strokeColor);
    }
  }

  if (_useFill && n >= 3) {
    // ear-clipping triangulation: handles concave polygons in any winding
    static int idx[_SHAPE_MAX_VERTS];
    for (int i = 0; i < n; i++) idx[i] = i;

    // normalize winding to positive signed area
    float area2 = 0.0f;
    for (int i = 0; i < n; i++) {
      Vector2 a = _shapeVerts[idx[i]];
      Vector2 b = _shapeVerts[idx[(i + 1) % n]];
      area2 += a.x * b.y - b.x * a.y;
    }
    if (area2 < 0.0f) {
      for (int i = 0; i < n / 2; i++) {
        int tmp = idx[i];
        idx[i] = idx[n - 1 - i];
        idx[n - 1 - i] = tmp;
      }
    }

    int count = n;
    int at = 0;
    int guard = n * n;
    while (count > 3 && guard-- > 0) {
      bool clipped = false;
      for (int attempt = 0; attempt < count; attempt++) {
        int ia = idx[(at + count - 1) % count];
        int ib = idx[at];
        int ic = idx[(at + 1) % count];
        Vector2 A = _shapeVerts[ia], B = _shapeVerts[ib], C = _shapeVerts[ic];

        if (_polyCross(A, B, C) <= 0.0f) { at = (at + 1) % count; continue; }

        bool earOk = true;
        for (int j = 0; j < count; j++) {
          int testIdx = idx[j];
          if (testIdx == ia || testIdx == ib || testIdx == ic) continue;
          if (_pointInTri(_shapeVerts[testIdx], A, B, C)) { earOk = false; break; }
        }
        if (!earOk) { at = (at + 1) % count; continue; }

        DrawTriangle(A, B, C, _fillColor);
        for (int j = at; j < count - 1; j++) idx[j] = idx[j + 1];
        count--;
        clipped = true;
        break;
      }
      if (!clipped) break; // remaining verts are collinear / degenerate
    }
    if (count == 3) {
      DrawTriangle(_shapeVerts[idx[0]], _shapeVerts[idx[1]], _shapeVerts[idx[2]], _fillColor);
    }
  }

  _shapeVertCount = -1;
}

static inline void endShape0(void) {
  _endShapeDraw();
}

static inline void endShape1(int mode) {
  _shapeModeClose = mode;
  _endShapeDraw();
}

// color utilities ///////////////////////////////////////////////////////////////////////

static inline uint32_t _packRGBA(int r, int g, int b, int a) {
  return ((uint32_t)(a & 0xFF) << 24) | ((uint32_t)(b & 0xFF) << 16) |
         ((uint32_t)(g & 0xFF) << 8) | (uint32_t)(r & 0xFF);
}

static inline uint8_t red(uint32_t c)     { return (uint8_t)((c >> 0) & 0xFF); }
static inline uint8_t green(uint32_t c)   { return (uint8_t)((c >> 8) & 0xFF); }
static inline uint8_t blue(uint32_t c)    { return (uint8_t)((c >> 16) & 0xFF); }
static inline uint8_t alpha(uint32_t c)   { return (uint8_t)((c >> 24) & 0xFF); }

static inline float brightness(uint32_t c) {
  float r = red(c) / 255.0f, g = green(c) / 255.0f, b = blue(c) / 255.0f;
  float mx = fmaxf(r, fmaxf(g, b));
  return mx * 100.0f;
}

static inline float saturation(uint32_t c) {
  float r = red(c) / 255.0f, g = green(c) / 255.0f, b = blue(c) / 255.0f;
  float mx = fmaxf(r, fmaxf(g, b));
  float mn = fminf(r, fminf(g, b));
  if (mx <= 0.0f) return 0.0f;
  return ((mx - mn) / mx) * 100.0f;
}

static inline float hue(uint32_t c) {
  float r = red(c) / 255.0f, g = green(c) / 255.0f, b = blue(c) / 255.0f;
  float mx = fmaxf(r, fmaxf(g, b));
  float mn = fminf(r, fminf(g, b));
  float delta = mx - mn;
  if (delta <= 0.0f) return 0.0f;
  float h;
  if (mx == r)      h = fmodf((g - b) / delta, 6.0f);
  else if (mx == g) h = (b - r) / delta + 2.0f;
  else              h = (r - g) / delta + 4.0f;
  h *= 60.0f;
  if (h < 0.0f) h += 360.0f;
  return h;
}

static inline uint32_t lerpColor(uint32_t c1, uint32_t c2, float amt) {
  int r = (int)lerp(red(c1), red(c2), amt);
  int g = (int)lerp(green(c1), green(c2), amt);
  int b = (int)lerp(blue(c1), blue(c2), amt);
  int a = (int)lerp(alpha(c1), alpha(c2), amt);
  return _packRGBA(r, g, b, a);
}

// hex / unhex: color <-> hex-string conversion
static inline const char *hex(uint32_t c) {
  static char buf[16];
  snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", alpha(c), red(c), green(c), blue(c));
  return buf;
}
static inline uint32_t unhex(const char *s) {
  return (uint32_t)strtoul(s, NULL, 16);
}

// blendColor (blend mode arg accepted; BLEND/ADD approximated) ////////////
enum {
  BLEND = 1, ADD = 2, SUBTRACT = 3, DARKEST = 4, LIGHTEN = 5,
  DIFFERENCE = 6, EXCLUSION = 7, MULTIPLY = 8, SCREEN = 9, OVERLAY = 10,
  HARD_LIGHT = 11, SOFT_LIGHT = 12, DODGE = 13, BURN = 14
};
static inline uint32_t blendColor(uint32_t c1, uint32_t c2, int mode) {
  switch (mode) {
    case ADD: {
      int r = red(c1) + red(c2), g = green(c1) + green(c2);
      int b = blue(c1) + blue(c2), a = alpha(c1) + alpha(c2);
      if (r > 255) r = 255; if (g > 255) g = 255;
      if (b > 255) b = 255; if (a > 255) a = 255;
      return _packRGBA(r, g, b, a);
    }
    default:
      return c1;
  }
}
static inline void blendMode(int mode) { (void)mode; }

// math //////////////////////////////////////////////////////////////////////////////////

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

// round/floor/ceil: Processing's float versions (float in, float out)
static inline float _pde_roundf(float v) { return floorf(v + 0.5f); }
static inline int _pde_roundi(int v) { return v; }
#define round(X) _Generic((X), int: _pde_roundi, default: _pde_roundf)(X)
static inline float _pde_floorf(float v) { return floorf(v); }
static inline int _pde_floori(int v) { return v; }
#define floor(X) _Generic((X), int: _pde_floori, default: _pde_floorf)(X)
static inline float _pde_ceilf(float v) { return ceilf(v); }
static inline int _pde_ceili(int v) { return v; }
#define ceil(X) _Generic((X), int: _pde_ceili, default: _pde_ceilf)(X)

static inline float constrain(float amt, float low, float high) {
  return (amt < low) ? low : ((amt > high) ? high : amt);
}

static inline float dist2(float x1, float y1, float x2, float y2) {
  float dx = x2 - x1;
  float dy = y2 - y1;
  return sqrtf(dx * dx + dy * dy);
}

static inline float dist3(float x1, float y1, float z1, float x2, float y2, float z2) {
  float dx = x2 - x1;
  float dy = y2 - y1;
  float dz = z2 - z1;
  return sqrtf(dx * dx + dy * dy + dz * dz);
}

static inline float sq(float value) {
  return value * value;
}

static inline float norm(float value, float start, float stop) {
  return (value - start) / (stop - start);
}

static inline float mag(float a, float b) {
  return sqrtf(a * a + b * b);
}

static inline float mag3(float a, float b, float c) {
  return sqrtf(a * a + b * b + c * c);
}

static inline float radians(float deg) {
  return deg * (PI / 180.0f);
}

static inline float degrees(float rad) {
  return rad * (180.0f / PI);
}

// transformations ///////////////////////////////////////////////////////////////////////

static inline void resetMatrix(void) {
  rlLoadIdentity();
}

static inline void rotateX(float radiansAngle) {
  rlRotatef(radiansAngle * 57.295779513f, 1.0f, 0.0f, 0.0f);
}

static inline void rotateY(float radiansAngle) {
  rlRotatef(radiansAngle * 57.295779513f, 0.0f, 1.0f, 0.0f);
}

static inline void rotateZ(float radiansAngle) {
  rlRotatef(radiansAngle * 57.295779513f, 0.0f, 0.0f, 1.0f);
}

static inline void shearX(float angle) {
  float t = tanf(angle);
  float m[16] = { 1.0f, t, 0.0f, 0.0f,
                  0.0f, 1.0f, 0.0f, 0.0f,
                  0.0f, 0.0f, 1.0f, 0.0f,
                  0.0f, 0.0f, 0.0f, 1.0f };
  rlMultMatrixf(m);
}

static inline void shearY(float angle) {
  float t = tanf(angle);
  float m[16] = { 1.0f, 0.0f, 0.0f, 0.0f,
                  t, 1.0f, 0.0f, 0.0f,
                  0.0f, 0.0f, 1.0f, 0.0f,
                  0.0f, 0.0f, 0.0f, 1.0f };
  rlMultMatrixf(m);
}

// PMatrix: snapshot of the current transform matrix (raylib layout) ///////
typedef Matrix PMatrix;

static inline PMatrix getMatrix(void) {
  return rlGetMatrixModelview();
}

static inline void applyMatrix(PMatrix m) {
  rlMultMatrixf((float *)&m);
}

// 3D primitives (OPENGL sketches) /////////////////////////////////////////
// The canvas renders through an orthographic projection, so sphere() draws
// under a temporary perspective projection (Processing's default camera
// looks down -z from a distance proportional to the object size).
static int _sphereDetail = 24;

// camera() state: applied inside _pde_begin3d so box()/sphere() keep view
static bool _customCameraSet = false;
static Vector3 _camEye, _camCenter, _camUpChosen;

static inline void camera0(void) { _customCameraSet = false; }
static inline void camera9(float eyeX, float eyeY, float eyeZ,
                           float centerX, float centerY, float centerZ,
                           float upX, float upY, float upZ) {
  _customCameraSet = true;
  _camEye = (Vector3){ eyeX, eyeY, eyeZ };
  _camCenter = (Vector3){ centerX, centerY, centerZ };
  _camUpChosen = (Vector3){ upX, upY, upZ };
}

#define CAMERA_CHOOSER(_1, _2, _3, _4, _5, _6, _7, _8, _9, NAME, ...) NAME
#define camera(...) CAMERA_CHOOSER(__VA_ARGS__, camera9, DUMMY, DUMMY, DUMMY, DUMMY, DUMMY, DUMMY, DUMMY, DUMMY, DUMMY)(__VA_ARGS__)

static inline void _pde_begin3d(float radius) {
  rlDrawRenderBatchActive(); // flush queued 2D verts before swapping proj
  float aspect = (float)width / (float)(height > 0 ? height : 1);
  float camZ = radius * 4.0f + 100.0f;
  Matrix proj = MatrixPerspective(60.0f * DEG2RAD, aspect,
                                  0.01f, camZ + radius * 10.0f + 200.0f);
  rlSetMatrixProjection(proj);
  Matrix mv = rlGetMatrixModelview();
  rlPushMatrix();
  rlLoadIdentity();
  if (_customCameraSet) {
    Matrix view = MatrixLookAt(_camEye, _camCenter, _camUpChosen);
    rlMultMatrixf((float *)&view);
    rlMultMatrixf((float *)&mv);   // keep the sketch's translate/rotate stack
  } else {
    rlMultMatrixf((float *)&mv);   // keep the sketch's translate/rotate stack
    rlTranslatef(0.0f, 0.0f, -camZ);
  }
}

static inline void _pde_end3d(void) {
  rlPopMatrix();
  rlDrawRenderBatchActive();     // flush 3D verts under perspective proj
  rlSetMatrixProjection(MatrixOrtho(0.0f, (float)width, (float)height, 0.0f,
                                    0.01f, 1000.0f));
}

static inline void sphereDetail(int n) {
  if (n >= 3 && n <= 128) _sphereDetail = n;
}

static inline void sphere(float r) {
  if (!_useFill && !_useStroke) return;
  _pde_begin3d(r);
  Vector3 c = { 0.0f, 0.0f, 0.0f };
  if (_useFill) DrawSphere(c, r, _fillColor);
  if (_useStroke) DrawSphereWires(c, r, _sphereDetail, _sphereDetail, _strokeColor);
  _pde_end3d();
}

// box(w, h, d) / box(size): axis-aligned cube under the same temporary
// perspective projection as sphere(); the size drives the camera distance.
static inline void box3(float w, float h, float d) {
  if (!_useFill && !_useStroke) return;
  float r = 0.5f * sqrtf(w * w + h * h + d * d);
  _pde_begin3d(r > 0.01f ? r : 1.0f);
  Vector3 c = { 0.0f, 0.0f, 0.0f };
  if (_useFill) DrawCube(c, w, h, d, _fillColor);
  if (_useStroke) DrawCubeWires(c, w, h, d, _strokeColor);
  _pde_end3d();
}
static inline void box1(float s) { box3(s, s, s); }

#define BOX_CHOOSER(_1, _2, _3, NAME, ...) NAME
#define box(...) BOX_CHOOSER(__VA_ARGS__, box3, DUMMY, box1, DUMMY)(__VA_ARGS__)

// normal(nx, ny, nz) / normal(nx, ny) — recorded for lighting; harmless no-op
// in a flat-shaded renderer (kept so beginShape(RENDER) sketches parse).
static inline void normal3(float nx, float ny, float nz) { (void)nx; (void)ny; (void)nz; }
static inline void normal2(float nx, float ny) { normal3(nx, ny, 0.0f); }

#define NORMAL_CHOOSER(_1, _2, _3, NAME, ...) NAME
#define normal(...) NORMAL_CHOOSER(__VA_ARGS__, normal3, normal2, DUMMY)(__VA_ARGS__)

// Vector math /////////////////////////////////////////////////////////////////////////////////

typedef struct {
  float x;
  float y;
  float z;
} PVector;

static inline PVector pvector2D(float x, float y) {
  return (PVector){ x, y, 0.0f };
}

static inline PVector pvector3D(float x, float y, float z) {
  return (PVector){ x, y, z };
}

static inline PVector pvector_add(PVector v1, PVector v2) {
  return (PVector){ v1.x + v2.x, v1.y + v2.y, v1.z + v2.z };
}

static inline PVector pvector_sub(PVector v1, PVector v2) {
  return (PVector){ v1.x - v2.x, v1.y - v2.y, v1.z - v2.z };
}

static inline PVector pvector_mult(PVector v, float n) {
  return (PVector){ v.x * n, v.y * n, v.z * n };
}

static inline PVector pvector_div(PVector v, float n) {
  if (n == 0.0f) return v;
  return (PVector){ v.x / n, v.y / n, v.z / n };
}

static inline float pvector_magSq(PVector v) {
  return (v.x * v.x + v.y * v.y + v.z * v.z);
}

static inline float pvector_mag(PVector v) {
  return sqrtf(pvector_magSq(v));
}

static inline float pvector_dist(PVector v1, PVector v2) {
  return sqrtf(powf(v1.x - v2.x, 2) + powf(v1.y - v2.y, 2) + powf(v1.z - v2.z, 2));
}

static inline float pvector_dot(PVector v1, PVector v2) {
  return (v1.x * v2.x + v1.y * v2.y + v1.z * v2.z);
}

static inline PVector pvector_cross(PVector v1, PVector v2) {
  return (PVector){
    v1.y * v2.z - v1.z * v2.y,
    v1.z * v2.x - v1.x * v2.z,
    v1.x * v2.y - v1.y * v2.x
  };
}

static inline PVector pvector_normalize(PVector v) {
  float m = pvector_mag(v);
  if (m != 0.0f && m != 1.0f) {
    return pvector_div(v, m);
  }
  return v;
}

static inline PVector pvector_limit(PVector v, float max) {
  if (pvector_magSq(v) > max * max) {
    return pvector_mult(pvector_normalize(v), max);
  }
  return v;
}

static inline PVector pvector_setMag(PVector v, float len) {
  return pvector_mult(pvector_normalize(v), len);
}

static inline float pvector_heading(PVector v) {
  return atan2f(v.y, v.x);
}

static inline PVector pvector_rotate(PVector v, float theta) {
  float c = cosf(theta);
  float s = sinf(theta);
  return (PVector){ v.x * c - v.y * s, v.x * s + v.y * c, v.z };
}

static inline PVector pvector_lerp(PVector v1, PVector v2, float amt) {
  return (PVector){
    v1.x + amt * (v2.x - v1.x),
    v1.y + amt * (v2.y - v1.y),
    v1.z + amt * (v2.z - v1.z)
  };
}

static inline PVector pvector_random2D(void) {
  float angle = ((float)rand() / (float)RAND_MAX) * 2.0f * PI;
  return (PVector){ cosf(angle), sinf(angle), 0.0f };
}

static inline PVector pvector_fromAngle(float angle) {
  return (PVector){ cosf(angle), sinf(angle), 0.0f };
}

static inline PVector pvector_copy(PVector v) {
  return v;
}

// heap allocation for "new PVector[n]" (transpiler rewrites to this)
static inline PVector *pvector_array_new(int n) {
  PVector *arr = (PVector *)calloc((size_t)(n > 0 ? n : 1), sizeof(PVector));
  return arr;
}

// heap allocation for "new TYPE[n]" of any element type (zero-initialized,
// registered so expand() knows the old element count)
static inline void *_pde_array_new(int n, size_t elemSize) {
  void *p = calloc((size_t)(n > 0 ? n : 1), elemSize);
  _pde_arr_register(p, (size_t)(n > 0 ? n : 0), elemSize);
  return p;
}

// defer //////////////////////////////////////////////////////////////////

static inline void destroyProcessing(void) {
  if (pixels) MemFree(pixels);
  if (_pixelsTexture.id > 0) UnloadTexture(_pixelsTexture);
  if (_canvas.id > 0) UnloadRenderTexture(_canvas);
}

#endif

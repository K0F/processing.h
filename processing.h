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
#include <sys/time.h>
// NOTE: <time.h> intentionally not included: sketches often declare a global
// named "time", which would collide with time(). <sys/time.h> provides
// gettimeofday() without declaring any symbol named "time".

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
  CHORD = 2,
  PIE = 3
};

static int width = 814;
static int height = 576;
static int mouseX = 0;
static int mouseY = 0;
static bool mousePressed = false;

typedef Font PFont;
typedef RenderTexture2D PGraphics;

static PFont _currentFont;
static float _textSizeState = 14.0f;
static float _textSpacing = 1.0f;
static Color _fillColor = { 255, 255, 255, 255 };
static Color _strokeColor = { 0, 0, 0, 255 };
static bool _useFill = true;
static bool _useStroke = true;
static float _strokeW = 1.0f;
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

// event callbacks (weak: defined only when the sketch provides them) /////
__attribute__((weak)) void keyPressed_event(void);
__attribute__((weak)) void keyReleased_event(void);
__attribute__((weak)) void mousePressed_event(void);
__attribute__((weak)) void mouseReleased_event(void);

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

static Font main_font;
static float current_text_size = 14.0f; 

static char _nfBuffers[4][64];
static int _nfBufferIndex = 0;
int frameCount = 0;

Color *pixels = NULL;
static Texture2D _pixelsTexture;
static RenderTexture2D _canvas;

// macros ////////////////////////////////////////////////////////////////
#define str(...) TextFormat(__VA_ARGS__)

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

// abs: type-preserving so integer array subscripts stay integers
static inline int _pde_absi(int v) { return v < 0 ? -v : v; }
#define abs(X) _Generic((X), int: _pde_absi, default: fabsf)(X)

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

static inline void println(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

///////////////////////////////////////////////////////////////////////////////////////////

static inline void load_default_font(void) {
  main_font = LoadFontEx("terminus.ttf", current_text_size, NULL, 0);

  if (main_font.texture.id <= 0) {
    main_font = LoadFontEx("/home/kof/src/RaylibProcessing/terminus.ttf", current_text_size, NULL, 0);
  }

  if (main_font.texture.id <= 0) {
    main_font = GetFontDefault();
  }

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

static _PdeDateTime _nowParts(void) {
  static struct timeval tv;
  gettimeofday(&tv, NULL);
  long long secs = (long long)tv.tv_sec;
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
}

static inline void beginDraw(void) {
  width = GetScreenWidth();
  height = GetScreenHeight();
  mouseX = GetMouseX();
  mouseY = GetMouseY();
  mousePressed = IsMouseButtonDown(MOUSE_BUTTON_LEFT);

  _pumpEvents();

  BeginDrawing();

  // all sketch drawing goes into the offscreen canvas
  BeginTextureMode(_canvas);
  ClearBackground(current_background_color);
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

static inline void image(PGraphics pg, float x, float y) {
  DrawTextureRec(pg.texture, (Rectangle){ 0, 0, (float)pg.texture.width, (float)-pg.texture.height }, (Vector2){ x, y }, WHITE);
}

// transformations (basic) /////////////////////////////////////////////////////

static inline void pushMatrix(void) { rlPushMatrix(); }
static inline void popMatrix(void) { rlPopMatrix(); }
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

// polygon engine (beginShape / vertex / endShape) //////////////////////////////////////

static inline void beginShape1(int mode) {
  (void)mode;
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

// math //////////////////////////////////////////////////////////////////////////////////

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

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
  rlMultMatrixf((float *)&mv);   // keep the sketch's translate/rotate stack
  rlTranslatef(0.0f, 0.0f, -camZ);
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

#ifndef PROCESSING_H
#define PROCESSING_H

#include "raylib.h"
#include "rlgl.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Pokud v raylibu nebo jinde nemáš definované PI:
#ifndef PI
#define PI 3.14159265358979323846f
#endif

// Přidat: Makro, které přeloží provizorní size_converted na tvou raylib/processing size funkci
#define size_converted(w, h) size(w, h, "Pde2C Engine")


static int width = 0;
static int height = 0;
static int mouseX = 0;
static int mouseY = 0;
static bool mousePressed = false;

typedef Font PFont;
typedef RenderTexture2D PGraphics;

static PFont _currentFont;
static float _textSizeState = 12.0f;
static float _textSpacing = 1.0f;
static Color _fillColor = { 255, 255, 255, 255 };
static Color _strokeColor = { 0, 0, 0, 255 };
static bool _useFill = true;
static bool _useStroke = true;
static float _strokeW = 1.0f;

static char _nfBuffers[4][64];
static int _nfBufferIndex = 0;
int frameCount = 0;

Color *pixels = NULL;
static Texture2D _pixelsTexture;

static inline void size(int w, int h, const char *title) {
    InitWindow(w, h, title);
    SetTargetFPS(60);
    _currentFont = GetFontDefault();
    
    width = w;
    height = h;
    
    pixels = (Color *)MemAlloc(w * h * sizeof(Color));
    Image blank = GenImageColor(w, h, BLANK);
    _pixelsTexture = LoadTextureFromImage(blank);
    UnloadImage(blank);
}

static inline void beginDraw(void) {
    width = GetScreenWidth();
    height = GetScreenHeight();
    mouseX = GetMouseX();
    mouseY = GetMouseY();
    mousePressed = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    
    BeginDrawing();
}

static inline void endDraw(void) {
    EndDrawing();
    frameCount++;
}

static inline void background(int gray) {
    ClearBackground((Color){ gray, gray, gray, 255 });
}

static inline void backgroundRGB(int r, int g, int b) {
    ClearBackground((Color){ r, g, b, 255 });
}

static inline PFont loadFont(const char *filename, int fontSize) {
    PFont font = LoadFontEx(filename, fontSize, NULL, 0);
    SetTextureFilter(font.texture, TEXTURE_FILTER_POINT);
    return font;
}

static inline void textFont(PFont font) {
    _currentFont = font;
}

static inline void textSize(float size) {
    _textSizeState = size;
}

static inline void text(const char *str, float x, float y) {
    if (_useFill) {
        DrawTextEx(_currentFont, str, (Vector2){ (int)x, (int)y }, (float)((int)_textSizeState), _textSpacing, _fillColor);
    }
}

static inline void save(const char *filename) {
    TakeScreenshot(filename);
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
    TakeScreenshot(buffer);
}

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

static inline void loadPixels(void) {
    Image img = LoadImageFromScreen();
    Color *colors = LoadImageColors(img);
    int count = width * height;
    for (int i = 0; i < count; i++) {
        pixels[i] = colors[i];
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

static inline void pushMatrix(void) { rlPushMatrix(); }
static inline void popMatrix(void) { rlPopMatrix(); }
static inline void translate(float x, float y) { rlTranslatef(x, y, 0.0f); }
static inline void rotate(float radians) { rlRotatef(radians * 57.295779513f, 0.0f, 0.0f, 1.0f); }
static inline void scale(float s) { rlScalef(s, s, 1.0f); }

// fill ////////////////////////////

static inline void fill4(float r, float g, float b, float a) {
    fill4(r, g, b, a);
}

static inline void fill3(float r, float g, float b) {
    fill4(r, g, b, 255.0f);
}

static inline void fill2(float gray, float alpha) {
    fill4(gray, gray, gray, alpha);
}

static inline void fill1(float gray) {
    fill4(gray, gray, gray, 255.0f);
}

#define FILL_CHOOSER(_1, _2, _3, _4, NAME, ...) NAME
#define fill(...) FILL_CHOOSER(__VA_ARGS__, fill4, fill3, fill2, fill1)(__VA_ARGS__)

static inline void noFill(void) { _useFill = false; }

// stroke /////////////////////////////////

static inline void stroke4(float r, float g, float b, float a) {
}

static inline void stroke3(float r, float g, float b) {
    stroke4(r, g, b, 255.0f);
}

static inline void stroke2(float gray, float alpha) {
    stroke4(gray, gray, gray, alpha);
}

static inline void stroke1(float gray) {
    stroke4(gray, gray, gray, 255.0f);
}

// stroke macro
#define STROKE_CHOOSER(_1, _2, _3, _4, NAME, ...) NAME
#define stroke(...) STROKE_CHOOSER(__VA_ARGS__, stroke4, stroke3, stroke2, stroke1)(__VA_ARGS__)

static inline void noStroke(void) { _useStroke = false; }


static inline void strokeWeight(float weight) {
    _strokeW = weight;
}


// line //////////////////
//
static inline void line6(float x1, float y1, float z1, float x2, float y2, float z2) {
    if (_useStroke) {
        if (_strokeW <= 1.0f) {
            DrawLine((int)x1, (int)y1, (int)x2, (int)y2, _strokeColor);
        } else {
            DrawLineEx((Vector2){ x1, y1 }, (Vector2){ x2, y2 }, _strokeW, _strokeColor);
        }
    }
}

// 4 argumenty: line(x1, y1, x2, y2) -> 2D verze, která tiše doplní Z souřadnice jako 0.0f
static inline void line4(float x1, float y1, float x2, float y2) {
    line6(x1, y1, 0.0f, x2, y2, 0.0f);
}

// Makro-výběrčí: Podle počtu argumentů skočí buď do line6 (3D) nebo line4 (2D)
#define LINE_CHOOSER(_1, _2, _3, _4, _5, _6, NAME, ...) NAME
#define line(...) LINE_CHOOSER(__VA_ARGS__, line6, dummy_error, line4)(__VA_ARGS__)

static inline void rect(float x, float y, float w, float h) {
    if (_useFill) {
        DrawRectangleRec((Rectangle){ x, y, w, h }, _fillColor);
    }
    if (_useStroke) {
        if (_strokeW <= 1.0f) {
            DrawRectangleLines((int)x, (int)y, (int)w, (int)h, _strokeColor);
        } else {
            Vector2 topL = { x, y }; Vector2 topR = { x + w, y };
            Vector2 botR = { x + w, y + h }; Vector2 botL = { x, y + h };
            DrawLineEx(topL, topR, _strokeW, _strokeColor);
            DrawLineEx(topR, botR, _strokeW, _strokeColor);
            DrawLineEx(botR, botL, _strokeW, _strokeColor);
            DrawLineEx(botL, topL, _strokeW, _strokeColor);
        }
    }
}

static inline void ellipse(float x, float y, float w, float h) {
    float rX = w / 2.0f; float rY = h / 2.0f;
    if (_useFill) {
        DrawEllipse((int)x, (int)y, rX, rY, _fillColor);
    }
    if (_useStroke) {
        if (_strokeW <= 1.0f) {
            DrawEllipseLines((int)x, (int)y, rX, rY, _strokeColor);
        } else {
            if (rX == rY) {
                DrawRing((Vector2){x, y}, rX - (_strokeW/2), rX + (_strokeW/2), 0, 360, 36, _strokeColor);
            } else {
                DrawEllipseLines((int)x, (int)y, rX, rY, _strokeColor);
            }
        }
    }
}

static inline float lerp(float start, float stop, float amt) {
    return start + amt * (stop - start);
}

static inline float map(float value, float start1, float stop1, float start2, float stop2) {
    return start2 + (stop2 - start2) * ((value - start1) / (stop1 - start1));
}

// Vector math /////////////////////

typedef struct {
    float x;
    float y;
    float z;
} PVector;



static inline PVector pvector3D(float x, float y, float z) {
    return (PVector){ x, y, z };
}
#define pvector2D(x, y) pvector3D(x, y, 0.0f)

#define PV_CHOOSER(_1, _2, _3, NAME, ...) NAME
#define pvector(...) PV_CHOOSER(__VA_ARGS__, pvector3D, pvector2D)(__VA_ARGS__)

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




static inline void destroyProcessing(void) {
    if (pixels) MemFree(pixels);
    UnloadTexture(_pixelsTexture);
}

#endif

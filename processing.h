#ifndef PROCESSING_H
#define PROCESSING_H

#include "raylib.h"
#include "rlgl.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

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

static inline void fill(int gray, int alpha) {
    _useFill = true;
    _fillColor = (Color){ gray, gray, gray, alpha };
}

static inline void fillRGB(int r, int g, int b, int a) {
    _useFill = true;
    _fillColor = (Color){ r, g, b, a };
}

static inline void noFill(void) { _useFill = false; }

static inline void stroke(int gray, int alpha) {
    _useStroke = true;
    _strokeColor = (Color){ gray, gray, gray, alpha };
}

static inline void strokeRGB(int r, int g, int b, int a) {
    _useStroke = true;
    _strokeColor = (Color){ r, g, b, a };
}

static inline void noStroke(void) { _useStroke = false; }

static inline void strokeWeight(float weight) {
    _strokeW = weight;
}

static inline void line(float x1, float y1, float x2, float y2) {
    if (_useStroke) {
        if (_strokeW <= 1.0f) {
            DrawLine((int)x1, (int)y1, (int)x2, (int)y2, _strokeColor);
        } else {
            DrawLineEx((Vector2){ x1, y1 }, (Vector2){ x2, y2 }, _strokeW, _strokeColor);
        }
    }
}

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

static inline void destroyProcessing(void) {
    if (pixels) MemFree(pixels);
    UnloadTexture(_pixelsTexture);
}

#endif

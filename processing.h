#ifndef PROCESSING_H
#define PROCESSING_H

#include "raylib.h"
#include "rlgl.h"
#include <stddef.h>
#include <stdbool.h>

typedef Font PFont; 

static PFont _currentFont;
static float _textSizeState = 12.0f;
static float _textSpacing   = 1.0f;

static Color _fillColor   = { 255, 255, 255, 255 };
static Color _strokeColor = { 0, 0, 0, 255 };
static bool  _useFill     = true;
static bool  _useStroke   = true;
static float _strokeW     = 1.0f;

static inline void size(int width, int height, const char *title) {
    InitWindow(width, height, title);
    SetTargetFPS(60);
    _currentFont = GetFontDefault();
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

#endif // PROCESSING_H

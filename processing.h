/*
processing.h -- minimal Processing-like API on top of raylib (C11)
author: Kryštof Pešek, 2025

howto: It requires raylib (with rlgl). Link with -lraylib -lm -lpthread -ldl (platform dependent).
Define PROCESSING_IMPLEMENTATION in one C file before including to provide implementation.
*/

#ifndef PROCESSING_H
#define PROCESSING_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "raylib.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef Color PColor;
typedef struct { Texture2D tex; int width; int height; } PImage;

/* Globals (provided by implementation) */
extern int width, height;
extern float mouseX, mouseY;
extern int pmouseX, pmouseY;
extern int mouseButton;
extern int keyIsPressed;
extern int frameCount;

/* Modes */
enum { CORNER = 0, CENTER = 1 };

/* Core API (canonical) */
int size_with_title(int w, int h, const char *title);
int processing_run(void);

void background_rgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a);

void stroke(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
void noStroke(void);
void fill(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
void noFill(void);

void point(float x, float y);
void line(float x1, float y1, float x2, float y2);
void rect(float x, float y, float w, float h);
void ellipse(float x, float y, float rx, float ry);
void triangle(float x1, float y1, float x2, float y2, float x3, float y3);

/* Transforms */
void pushMatrix(void);
void popMatrix(void);
void translatef(float x, float y);
void rotate(float degrees);
void scale(float sx, float sy);

/* Images */
PImage loadImage(const char *path);
void unloadImage(PImage img);
void drawImage(PImage img, float x, float y, float w, float h);

/* Input */
int mousePressed(void);
int keyPressed(void);

/* Utils */
float random(float min, float max);
long millis(void);

/* User functions (Processing sketches use void setup() / void draw()) */
extern void setup(void);
extern void draw(void);

/* Compatibility macros/wrappers to accept Processing-style calls:
   - size(w,h) and size(w,h,title)
   - background(v), background(r,g,b), background(r,g,b,a)
*/
static inline int size_with_title_wrapper(int w, int h, const char *title) {
	width = w;
	height = h;
    return size_with_title(w, h, title ? title : "processing.h");
}
#define size(...) _SIZE_DISPATCH(__VA_ARGS__, size3, size2)(__VA_ARGS__)
#define _SIZE_DISPATCH(_1,_2,_3,NAME,...) NAME
static inline int size2(int w, int h) { return size_with_title_wrapper(w,h,NULL); }
static inline int size3(int w, int h, const char *title) { return size_with_title_wrapper(w,h,title); }

/* background overloads */
static inline void background1(unsigned char v) { background_rgba(v,v,v,255); }
static inline void background3(unsigned char r, unsigned char g, unsigned char b) { background_rgba(r,g,b,255); }
static inline void background4(unsigned char r, unsigned char g, unsigned char b, unsigned char a) { background_rgba(r,g,b,a); }
#define background(...) _BG_DISPATCH(__VA_ARGS__, background4, background3, background1)(__VA_ARGS__)
#define _BG_DISPATCH(_1,_2,_3,_4,NAME,...) NAME

/* Also provide short aliases matching earlier header names */
#define size_simple size2
#define background_rgba_alias background_rgba


// TODO: add third dimmension macro wrapper
typedef union PVector
{
	float v[2];
	struct { float x, y; };
} PVector;

//---------------------------------------------------------
// BLEND MODE:
//		Change the math applied to colors when drawn on top of each other
typedef enum BLEND_MODE
{
	ALPHA,		// BLEND
	ADD,		// ADD
	SUBTRACT,	// DIFFERENCE
	MULTIPLY,	// MULTIPLY
	MIN,		// DARKEST
	MAX		// LIGHTEST
} BLEND_MODE;

/* Implementation */
#ifdef PROCESSING_IMPLEMENTATION

/* Internal globals */
int width = 800;
int height = 450;
static const char *g_window_title = "processing";
static int g_window_inited = 0;
static Vector2 g_mouse_prev = {0,0};
float mouseX = 0.f;
float mouseY = 0.f;
int pmouseX = 0;
int pmouseY = 0;
int mouseButton = 0;
int keyIsPressed = 0;
int frameCount = 0;
static Color g_stroke = {0,0,0,255};
static int g_doStroke = 1;
static Color g_fill = {255,255,255,255};
static int g_doFill = 1;
static int g_rectMode = CORNER;

/* Require rlgl */
#include "rlgl.h"

int size_with_title(int w, int h, const char *title) {
    if (w > 0) width = w;
    if (h > 0) height = h;
    if (title && title[0]) g_window_title = title;
    return 0;
}

int processing_run(void) {
    if (!g_window_inited) {
        InitWindow(width, height, g_window_title);
        SetTargetFPS(60);
        g_window_inited = 1;
    }

    /* call user setup (sketch must provide) */
    setup();

    while (!WindowShouldClose()) {
        Vector2 m = GetMousePosition();
        pmouseX = (int)g_mouse_prev.x;
        pmouseY = (int)g_mouse_prev.y;
        mouseX = m.x; mouseY = m.y;
        g_mouse_prev = m;
        mouseButton = IsMouseButtonDown(MOUSE_LEFT_BUTTON) ? 1 :
                      (IsMouseButtonDown(MOUSE_RIGHT_BUTTON) ? 2 :
                       (IsMouseButtonDown(MOUSE_MIDDLE_BUTTON) ? 3 : 0));

        /* keyIsPressed best-effort: try GetKeyPressed (if available) */
#ifdef __HAS_GETKEYPRESSED
        keyIsPressed = GetKeyPressed() ? 1 : 0;
#else
        keyIsPressed = (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_LEFT_CONTROL) ||
                        IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_ENTER)) ? 1 : 0;
#endif

        BeginDrawing();
        ClearBackground(BLANK);

        rlPushMatrix();
        draw();
        rlPopMatrix();

        EndDrawing();
        frameCount++;
    }

    CloseWindow();
    return 0;
}

/* Background */
void background_rgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    ClearBackground((Color){r,g,b,a});
}

/* Styles */
void stroke(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    g_stroke = (Color){r,g,b,a}; g_doStroke = 1;
}
void noStroke(void) { g_doStroke = 0; }
void fill(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    g_fill = (Color){r,g,b,a}; g_doFill = 1;
}
void noFill(void) { g_doFill = 0; }

/* Primitives */
void point(float x, float y) { DrawPixel((int)x,(int)y, g_doStroke ? g_stroke : WHITE); }
void line(float x1, float y1, float x2, float y2) { DrawLine((int)x1,(int)y1,(int)x2,(int)y2, g_doStroke ? g_stroke : WHITE); }
void rect(float x, float y, float w, float h) {
    if (g_rectMode == CENTER) { x -= w*0.5f; y -= h*0.5f; }
    if (g_doFill) DrawRectangle((int)x,(int)y,(int)w,(int)h, g_fill);
    if (g_doStroke) DrawRectangleLines((int)x,(int)y,(int)w,(int)h, g_stroke);
}
void ellipse(float x, float y, float rx, float ry) {
    if (g_doFill) DrawEllipse((int)x,(int)y,(float)rx,(float)ry, g_fill);
    if (g_doStroke) DrawEllipseLines((int)x,(int)y,(float)rx,(float)ry, g_stroke);
}
void triangle(float x1,float y1,float x2,float y2,float x3,float y3) {
    if (g_doFill) { Vector2 pts[3]={{x1,y1},{x2,y2},{x3,y3}}; DrawTriangle(pts[0],pts[1],pts[2], g_fill); }
    if (g_doStroke) {
        DrawLine((int)x1,(int)y1,(int)x2,(int)y2,g_stroke);
        DrawLine((int)x2,(int)y2,(int)x3,(int)y3,g_stroke);
        DrawLine((int)x3,(int)y3,(int)x1,(int)y1,g_stroke);
    }
}

/* Transforms */
void pushMatrix(void){ rlPushMatrix(); }
void popMatrix(void){ rlPopMatrix(); }
void translate(float x,float y){ rlTranslatef(x,y,0.0f); }
void rotate(float degrees){ rlRotatef(degrees,0.0f,0.0f,1.0f); }
void scale(float sx,float sy){ rlScalef(sx,sy,1.0f); }

/* Images */
PImage loadImage(const char *path) {
    PImage out = {0};
    Image img = LoadImage(path);
    if (img.data == NULL) return out;
    out.tex = LoadTextureFromImage(img); out.width = img.width; out.height = img.height;
    UnloadImage(img); return out;
}
void unloadImage(PImage img){ if (img.tex.id) UnloadTexture(img.tex); }
void drawImage(PImage img, float x,float y,float w,float h){
    Rectangle src={0,0,(float)img.width,(float)img.height};
    Rectangle dst={x,y,w,h}; Vector2 origin={0,0};
    DrawTexturePro(img.tex, src, dst, origin, 0.0f, WHITE);
}

/* Input & utils */
int mousePressed(void){ return mouseButton != 0; }
int keyPressed(void){ return keyIsPressed; }
float random(float min,float max){ if(min>=max) return min; return min + (float)rand()/(float)RAND_MAX*(max-min); }
long millis(void){ return (long)(GetTime()*1000.0); }

#endif /* PROCESSING_IMPLEMENTATION */

#ifdef __cplusplus
}
#endif

#endif /* PROCESSING_H */

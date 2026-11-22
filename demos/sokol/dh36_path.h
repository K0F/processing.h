// LLM maintained.
// Shared one-line path generator for the dh36 sokol demo and SVG exporter.
#ifndef DH36_PATH_H
#define DH36_PATH_H

#include <math.h>
#include <stdio.h>

#define PATH_W 814
#define PATH_H 576
#define NUM_RAYS 36
#define CIRCLE_SEGS 96
#define MAX_POINTS 2048
#define DH36_TAU (6.28318530717958647692f)

typedef struct { float x, y; } dh36_pt_t;

typedef struct {
    dh36_pt_t pts[MAX_POINTS];
    int n;
} dh36_path_t;

static inline void dh36_push(dh36_path_t* p, float x, float y) {
    if (p->n < MAX_POINTS) {
        p->pts[p->n].x = x;
        p->pts[p->n].y = y;
        p->n++;
    }
}

// One unbroken stroke: center -> two concentric decanic rings -> back to
// center -> each of the 36 rays out and back through the shared center.
static inline void dh36_build(dh36_path_t* p) {
    const float cx = (float)PATH_W * 0.5f;
    const float cy = (float)PATH_H * 0.5f;
    const float ray_len = (float)PATH_W / 1.5f;
    const float radii[2] = { 51.25f, 50.0f }; // original diameters 205/200 at half scale
    p->n = 0;
    dh36_push(p, cx, cy);

    // Ring detours ride along the +x ray, so no new visible connector appears.
    for (int k = 0; k < 2; k++) {
        dh36_push(p, cx + radii[k], cy);
        for (int i = 1; i <= CIRCLE_SEGS; i++) {
            const float a = (float)i / (float)CIRCLE_SEGS * DH36_TAU;
            dh36_push(p, cx + cosf(a) * radii[k], cy + sinf(a) * radii[k]);
        }
    }
    dh36_push(p, cx, cy);

    for (int i = 0; i < NUM_RAYS; i++) {
        const float a = (float)i / (float)NUM_RAYS * DH36_TAU;
        dh36_push(p, cx + cosf(a) * ray_len, cy + sinf(a) * ray_len);
        dh36_push(p, cx, cy);
    }
}

// Batik-style SVG with exactly one <path> (fill:none, one pen-down stroke).
static inline void dh36_write_svg(const dh36_path_t* p, const char* filename) {
    FILE* f = fopen(filename, "w");
    if (!f) {
        perror(filename);
        return;
    }
    fputs("<?xml version=\"1.0\"?>\n", f);
    fputs("<!DOCTYPE svg PUBLIC '-//W3C//DTD SVG 1.0//EN'\n", f);
    fputs("          'http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd'>\n", f);
    fputs("<svg xmlns:xlink=\"http://www.w3.org/1999/xlink\" style=\"fill-opacity:1; color-rendering:auto; color-interpolation:auto; text-rendering:auto; stroke:black; stroke-linecap:round; stroke-miterlimit:10; shape-rendering:auto; stroke-opacity:1; fill:black; stroke-dasharray:none; font-weight:normal; stroke-width:1; font-family:'Dialog'; font-style:normal; stroke-linejoin:miter; font-size:12px; stroke-dashoffset:0; image-rendering:auto;\" width=\"814\" height=\"576\" xmlns=\"http://www.w3.org/2000/svg\"\n >", f);
    fputs("<g\n   ><g style=\"stroke-linecap:round;\"\n     ><path style=\"fill:none;\" d=\"", f);
    fprintf(f, "M%.4f %.4f", p->pts[0].x, p->pts[0].y);
    for (int i = 1; i < p->n; i++) {
        fprintf(f, " L%.4f %.4f", p->pts[i].x, p->pts[i].y);
    }
    fputs("\"\n     /></g\n   ></g\n ></svg\n >\n", f);
    fclose(f);
    printf("wrote %s (%d points, 1 continuous stroke)\n", filename, p->n);
}

#endif // DH36_PATH_H

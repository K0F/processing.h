#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include "processing.h"
#include <math.h>

static int total_frames = 36;

void setup(void) {
    size(320, 240);
    frameRate(18);
}

void draw(void) {
    background(12);

    int f = frameCount;
    float t = (float)f / (float)total_frames;
    int is_pro = (f <= total_frames / 2);
    
    // Decanic phase angle
    float tau = is_pro ? (t * 2.0f) : (2.0f - t * 2.0f); // Palindromic triangle 0 -> 1 -> 0
    float angle = t * 2.0f * (float)M_PI;

    // Draw pixel grid (10px grid)
    stroke(25);
    for (int x = 0; x < width; x += 16) {
        line(x, 0, x, height);
    }
    for (int y = 0; y < height; y += 16) {
        line(0, y, width, y);
    }

    // Coordinate crosshair
    stroke(45);
    line(width / 2, 0, width / 2, height);
    line(0, height / 2, width, height / 2);

    // Draw Symplectic Orbit / Lissajous-Borges Knots
    pushMatrix();
    translate(width / 2, height / 2 - 10);
    noFill();

    // Past trail
    if (is_pro) {
        stroke(0, 255, 170); // Cyan/green for prograde
    } else {
        stroke(255, 60, 100); // Magenta/orange for retrograde
    }

    beginShape();
    int steps = 120;
    for (int i = 0; i < steps; i++) {
        float s = (float)i / (float)steps;
        float p_angle = s * 4.0f * (float)M_PI + angle;
        // Symplectic coupled phase space (x, p)
        float px = sinf(p_angle * 2.0f) * 85.0f * (0.3f + 0.7f * tau);
        float py = cosf(p_angle * 3.0f + (is_pro ? angle : -angle)) * 65.0f * (0.3f + 0.7f * tau);
        vertex(px, py);
    }
    endShape(OPEN);

    // Current phase head (particle)
    float cur_x = sinf(angle * 2.0f) * 85.0f * (0.3f + 0.7f * tau);
    float cur_y = cosf(angle * 3.0f + (is_pro ? angle : -angle)) * 65.0f * (0.3f + 0.7f * tau);
    fill(255);
    noStroke();
    rect(cur_x - 2, cur_y - 2, 5, 5);

    popMatrix();

    // Top status banner (Terminus pixel typography)
    fill(220);
    text(StringFormat("TIMEWARP dh36: %02d/36 [%s]", f, is_pro ? "+1 PRO" : "-1 RET"), 10, 18);
    
    fill(is_pro ? color(0, 255, 170) : color(255, 60, 100));
    text(StringFormat("sigma: %s | tau: %.3f", is_pro ? "dt>0" : "dt<0", tau), 10, 32);

    // Bottom telemetry bar
    fill(140);
    text("50Hz: 49.982 Hz  |  dS = 0.0000 b", 10, height - 12);
    text("drkota: Z0/P0", width - 95, height - 12);

    // Save frame to temporary frames folder
    saveFrame("/tmp/timewarp_frames/frame-####.png");

    if (f >= total_frames) {
        exit_sketch();
    }
}

int main(void) {
    setup();
    if (_windowInit == 0) size3(320, 240, "Timewarp dh36");
    while (!WindowShouldClose() && !_exitRequested) {
        beginDraw();
        if (_loopRunning || _redrawPending) {
            _redrawPending = false;
            draw();
        }
        endDraw();
    }
    return 0;
}

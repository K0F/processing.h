#include "processing.h"
#include <stdio.h>

float text_size = 15.0f;

int main(void) {
    size(814, 576, "Processing Raylib");

    PFont font = loadFont("terminus.ttf", text_size);

    char frameString[32];

    while (!WindowShouldClose()) {
        snprintf(frameString, sizeof(frameString), "SYS_FPS: %d", GetFPS());

        BeginDrawing();
        background(12);

        textSize(text_size);
        fill(140, 255);
        text("DEFAULT RENDER:", 40, 40);
        
        fillRGB(0, 255, 120, 255);
        text(frameString, 40, 65);

        textFont(font);
        
        textSize(text_size);
        fill(140, 255);
        text("EXTERNAL TTF VECTOR CONTEXT (CRISP):", 40, 150);

        textSize(text_size);
        fillRGB(80, 180, 255, 255);
        text("METRIC NODE_01", 40, 180);

        // Thin divider graphic blueprint lines
        stroke(60, 255);
        strokeWeight(1.0f);
        line(40, 240, 560, 240);

        fill(200, 255);
        noStroke();
        
        textSize(text_size);
        text("Vector rendering handles clean scale steps seamlessly.", 40, 270);
		pushMatrix();
		translate(width/2,height/2+100.0f);
		rotate(frameCount/60.0f * PI);
		rect(-50,-50,100,100);
		popMatrix();
        EndDrawing();

        saveFrame("fr#####.png");
        frameCount++;
    }

    UnloadFont(font);
    CloseWindow();
    return 0;
}

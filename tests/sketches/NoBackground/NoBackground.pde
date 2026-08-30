// Regression: when background() is never called, no background is drawn and
// prior frames persist (accumulating trails instead of a fresh clear).
void setup() {
  size(200, 200);
}

void draw() {
  fill(255, 0, 0);
  rect(mouseX, mouseY, 8, 8);
}
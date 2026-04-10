#include "Processing.h"


bool showPerspective = false;

void setup() {
  size(600, 360, P3D);  // make sure this enables 3D in your engine
  noFill();
  fill(255);
  noStroke();
}

void draw() {
  background(0);
  lights();

  float farVal = map(mouseX, 0, width, 120, 400);

  if (showPerspective) {
    perspective(PI / 3.0f, (float)width / (float)height, 10.0f, farVal);
  } else {
    ortho(-width / 2.0f, width / 2.0f,
          -height / 2.0f, height / 2.0f,
          10.0f, farVal);
  }

  translate(width / 2.0f, height / 2.0f, 0);
  rotateX(-PI / 6.0f);
  rotateY(PI / 3.0f);

  box(180);
}

void mousePressed() {
  showPerspective = !showPerspective;
}

#include "Processing.h"
namespace Processing {
// -- run once ----------------------------------------
void setup() {
  size(640, 360);
}

// -- looasdps forever -----------------------------------
void draw() {
  background(102);
  fill(255);
  ellipse(mouseX, mouseY, 40, 40);
}
} // namespace Processing

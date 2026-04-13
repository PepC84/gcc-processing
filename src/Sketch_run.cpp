#include "Processing.h"
namespace Processing {
// =============================================================================
// PerspectiveOrtho.cpp  --  ProcessingGL sketch
//
// Translated from "Perspective vs. Ortho" by the Processing Foundation.
// Move the mouse left/right to change the far clipping distance.
// Click to toggle between perspective and orthographic projection.
//
// NOTE: The IDE auto-translates Java keywords when building:
//   boolean -> bool     float(x) -> (float)(x)     null -> nullptr
// So the sketch below matches the original Java source exactly.
// =============================================================================

#include "Processing.h"



bool showPerspective = false;

void setup() {
    size(640, 360, P3D);
    noStroke();
    fill(255);
}

void draw() {
    lights();
    background(0);

    // Map mouseX to the far clipping plane distance
    float far = map((float)mouseX, 0, (float)width, 120, 400);

    if (showPerspective) {
        // Perspective: PI/3 (~60 deg) FOV, standard aspect, near=10, far=mapped
        perspective(PI / 3.0f, (float)width / (float)height, 10, far);
    } else {
        // Orthographic: centred on origin, same near/far
        ortho(-width / 2.0f, width / 2.0f,
              -height / 2.0f, height / 2.0f,
               10, far);
    }

    translate(width / 2.0f, height / 2.0f, 0);
    rotateX(-PI / 6.0f);
    rotateY( PI / 3.0f);
    box(180);
}

void mousePressed() {
    showPerspective = !showPerspective;
}

// namespace Processing
} // namespace Processing

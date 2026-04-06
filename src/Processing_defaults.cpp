// Processing_defaults.cpp
// ─────────────────────────────────────────────────────────────────────────
// Weak default implementations of all optional Processing event callbacks.
//
// On Linux/macOS: Processing.h declares these with __attribute__((weak))
// so they resolve to nullptr when unimplemented — this file isn't needed.
//
// On Windows (MinGW-w64): weak declarations in headers don't work, but
// __attribute__((weak)) on a *definition* in a .cpp file does work with
// MinGW's GCC.  The linker picks the sketch/IDE's real definition over
// these weak ones, so unimplemented callbacks become harmless no-ops.
//
// Build: always compile this file alongside Processing.cpp and your sketch.
//   g++ ... src/Processing.cpp src/Processing_defaults.cpp your_sketch.cpp ...
// ─────────────────────────────────────────────────────────────────────────
#include "Processing.h"

namespace Processing {

void keyPressed()          __attribute__((weak));
void keyReleased()         __attribute__((weak));
void keyTyped()            __attribute__((weak));
void mousePressed()        __attribute__((weak));
void mouseReleased()       __attribute__((weak));
void mouseClicked()        __attribute__((weak));
void mouseMoved()          __attribute__((weak));
void mouseDragged()        __attribute__((weak));
void mouseWheel(int delta) __attribute__((weak));
void windowMoved()         __attribute__((weak));
void windowResized()       __attribute__((weak));

// Weak definitions — overridden by any non-weak definition in the sketch/IDE
void keyPressed()          {}
void keyReleased()         {}
void keyTyped()            {}
void mousePressed()        {}
void mouseReleased()       {}
void mouseClicked()        {}
void mouseMoved()          {}
void mouseDragged()        {}
void mouseWheel(int)       {}
void windowMoved()         {}
void windowResized()       {}

} // namespace Processing

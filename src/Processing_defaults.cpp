// Processing_defaults.cpp
// ─────────────────────────────────────────────────────────────────────────────
// Default (empty) implementations of all optional Processing event callbacks.
//
// On Linux/macOS these are declared __attribute__((weak)) in Processing.h
// so this file is NOT needed — the linker resolves them to nullptr.
//
// On Windows (MinGW) weak symbols are not supported. This file provides empty
// fallback bodies for every callback. The sketch or IDE overrides them by
// providing its own non-weak definitions — the linker picks the non-default
// one when two definitions exist, as long as this file provides them as
// "default" via a linker trick.
//
// Build system note: compile this file LAST so the linker sees the sketch's
// real definitions first and uses those in preference.
//
// Usage in buildIDE.sh / build.sh (Windows only):
//   g++ ... src/Processing_defaults.cpp ...
// ─────────────────────────────────────────────────────────────────────────────

// Only active on Windows — Linux/macOS uses __attribute__((weak)) instead
#ifdef _WIN32

#include "Processing.h"

// Each function is wrapped in a "selectany" linkage so the linker drops it
// when a real definition is provided by the sketch/IDE.
namespace Processing {

// __declspec(selectany) makes these "pick any one" — if IDE.cpp or a sketch
// also defines them, the linker keeps only that one and discards these stubs.
__declspec(selectany) void keyPressed()          {}
__declspec(selectany) void keyReleased()         {}
__declspec(selectany) void keyTyped()            {}
__declspec(selectany) void mousePressed()        {}
__declspec(selectany) void mouseReleased()       {}
__declspec(selectany) void mouseClicked()        {}
__declspec(selectany) void mouseMoved()          {}
__declspec(selectany) void mouseDragged()        {}
__declspec(selectany) void mouseWheel(int)       {}
__declspec(selectany) void windowMoved()         {}
__declspec(selectany) void windowResized()       {}

} // namespace Processing

#endif // _WIN32

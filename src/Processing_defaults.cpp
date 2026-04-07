// Processing_defaults.cpp
// Default stub implementations for Windows (MinGW/MSVC).
// These are overridden by IDE.cpp or the user's sketch.
//
// Compile this alongside every binary on Windows:
//   g++ ... src/Processing_defaults.cpp ...

#ifdef _WIN32
#include "Processing.h"

namespace Processing {

// Default wireCallbacks() -- does nothing.
// IDE.cpp and user sketches override this by defining their own wireCallbacks()
// after all their event functions (mousePressed, keyPressed, etc.) are defined.
// The linker picks the non-default definition when one exists.
__declspec(selectany) void wireCallbacks() {
    // No-op default. The sketch or IDE provides the real implementation.
}

} // namespace Processing
#endif

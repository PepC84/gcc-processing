// =============================================================================
// Processing_defaults.cpp -- Default event stubs for Windows builds
//
// On Linux/macOS: event callbacks are declared __attribute__((weak)) in
// Processing.h, so undefined ones silently resolve to nullptr at link time.
//
// On Windows (MinGW): weak declarations in headers don't work, but weak
// DEFINITIONS in .cpp files DO work with __attribute__((weak)).
// This file provides weak no-op definitions for all callbacks.
//
// The IDE's wireCallbacks() (at the bottom of IDE.cpp) is a strong symbol
// and automatically wins over this weak stub at link time.
// User sketches that don't call wireCallbacks() at all get this no-op.
//
// Always compile this file on Windows:
//   g++ ... src/Processing_defaults.cpp ...
// =============================================================================

#ifdef _WIN32
#include "Processing.h"

namespace Processing {

// Weak definition -- overridden by IDE.cpp's strong wireCallbacks()
void __attribute__((weak)) wireCallbacks() {
    // Default: wire nothing. The IDE provides the real implementation.
}

} // namespace Processing
#endif

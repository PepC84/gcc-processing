// Processing_defaults.cpp
// =============================================================================
// Provides default (no-op) implementations needed for linking on Windows.
//
// On Linux/macOS: event callbacks use __attribute__((weak)) declared in
// Processing.h, so this file compiles to nothing -- the #ifdef guards it.
//
// On Windows (MinGW): wireCallbacks() must exist as a symbol.
// This weak definition is overridden by the real one in IDE.cpp.
// For user sketches that don't have their own wireCallbacks(), this no-op
// is used and events are simply not wired (which is fine for simple sketches).
//
// Always compile this file on ALL platforms -- it's safe and empty on Linux.
// =============================================================================

#ifdef _WIN32
#include "Processing.h"

namespace Processing {

// Weak definition -- the linker prefers IDE.cpp's strong version when present.
// For user sketches without wireCallbacks(), this no-op satisfies the linker.
void __attribute__((weak)) wireCallbacks() {}

} // namespace Processing
#endif

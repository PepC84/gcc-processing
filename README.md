# ProcessingGL

**A Processing-style creative coding framework & IDE — written in C++ with OpenGL**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux-lightgrey)](#setup--install)
[![Language](https://img.shields.io/badge/language-C%2B%2B17-orange)](https://isocpp.org/)
[![Built with Claude](https://img.shields.io/badge/built%20with-Claude%20AI-blueviolet)](https://www.anthropic.com)

---

## What is this?

`ProcessingGL` brings the simplicity of [Processing](https://processing.org/) to native C++.
Write `setup()` and `draw()` functions and get a real-time OpenGL window — no boilerplate, no engine overhead.
It ships with a built-in **live-coding IDE** so you can edit and run sketches without leaving the app.

**Key features:**
- `setup()` / `draw()` loop just like Processing
- Real-time OpenGL rendering via GLFW + GLEW
- Built-in IDE for live sketch editing
- Image loading (`stb_image`)
- TrueType font rendering (`stb_truetype`)
- Keyboard, mouse, and window event callbacks
- One-command install on Windows, macOS, and Linux

---

## Setup & Install

### Windows

> Requires [MSYS2](https://www.msys2.org/) — open the **MSYS2 MinGW64** terminal, then:

```bash
bash setup.sh
```

Or double-click `setup.bat` for instructions.

### macOS

> Requires [Homebrew](https://brew.sh) — install it first if you don't have it.

```bash
bash setup.sh
```

### Linux

```bash
bash setup.sh
```

Supports: **Arch**, **Ubuntu/Debian**, **Fedora**, **openSUSE**

---

## Build & Run

### The IDE (recommended)

| Platform       | Build            | Run        |
|----------------|------------------|------------|
| Windows        | `buildIDE.bat`   | `ide.exe`  |
| macOS / Linux  | `bash buildIDE.sh` | `./ide`  |

Open the IDE, write your sketch, and hit run.

### Build a Sketch directly

Edit `src/MySketch_gen.cpp` with your `setup()` and `draw()` code, then:

| Platform       | Build          | Run              |
|----------------|----------------|------------------|
| Windows        | `build.bat`    | `SketchApp.exe`  |
| macOS / Linux  | `bash build.sh` | `./SketchApp`   |

---

## Writing a Sketch

```cpp
#include "Processing.h"

namespace Processing {

void setup() {
    size(800, 600);
    background(30);
}

void draw() {
    fill(255, 100, 50);
    ellipse(mouseX, mouseY, 40, 40);
}

} // namespace Processing
```

---

## Project Structure

```
ProcessingGL/
├── src/
│   ├── Processing.h        # API header — include this in your sketch
│   ├── Processing.cpp      # Framework implementation
│   ├── IDE.cpp             # Built-in live-coding IDE
│   ├── MySketch_gen.cpp    # Your sketch goes here
│   ├── main.cpp            # Entry point
│   ├── stb_image.h         # Image loading (auto-downloaded by setup)
│   └── stb_truetype.h      # Font rendering (auto-downloaded by setup)
├── default.ttf             # Default font (auto-found/copied by setup)
├── config.json             # Default window/fps config
├── setup.sh                # macOS + Linux + Windows (MSYS2) installer
├── setup.bat               # Windows installer helper
├── build.sh / build.bat    # Build a sketch
├── buildIDE.sh / buildIDE.bat  # Build the IDE
└── run.sh                  # Build + run a sketch (Linux/macOS)
```

---

## Troubleshooting

**`GL/glew.h` not found (macOS)**
```bash
brew install glew glfw
```
Then use `bash buildIDE.sh` — it handles the Homebrew paths automatically.

**Linker errors: missing `mouseMoved` / `mouseClicked` / etc.**
These stubs are already in `src/IDE.cpp`. If you see them on a fresh clone, make sure you pulled the latest version.

**`-lGL` not found on macOS**
macOS uses `-framework OpenGL` instead of `-lGL`. The build scripts handle this automatically.

**Windows: `g++` not found**
Run `setup.sh` from the **MSYS2 MinGW64** terminal, or run `setup.bat` for guidance.

---

## Credits

- Original framework & IDE: [PepC84](https://github.com/PepC84)
- Windows support, macOS porting & tooling improvements: [ktorres0109](https://github.com/ktorres0109)
- Header libraries: [nothings/stb](https://github.com/nothings/stb)
- Built with assistance from [Claude AI](https://www.anthropic.com) (Anthropic)

---

## License

[MIT](LICENSE) — free to use, modify, and distribute.

#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# gcc-processing — Universal Setup Script
# Supports: Arch, Debian/Ubuntu, Fedora, openSUSE, macOS, Windows (MSYS2)
# Usage:
#   Linux/macOS : chmod +x setup.sh && ./setup.sh
#   Windows     : Open MSYS2 MinGW64 terminal and run: bash setup.sh
#   Windows     : Or just double-click setup.bat (auto-detects MSYS2)
# ═══════════════════════════════════════════════════════════════════════════

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# ── Colours ────────────────────────────────────────────────────────────────
R='\033[0;31m' G='\033[0;32m' Y='\033[1;33m'
B='\033[0;34m' C='\033[0;36m' M='\033[0;35m' N='\033[0m'
info() { echo -e "${B}[INFO]${N} $1"; }
ok()   { echo -e "${G}[ OK ]${N} $1"; }
warn() { echo -e "${Y}[WARN]${N} $1"; }
step() { echo -e "\n${C}━━━ $1${N}"; }
die()  { echo -e "${R}[ERR ]${N} $1";
         [[ $WIN -eq 1 ]] && read -r -p "Press Enter to close..." _
         exit 1; }
ask()  { echo -e "${M} $1${N}"; }

echo ""
echo -e "${C} ╔════════════════════════════════════════════╗"
echo -e " ║    gcc-processing — Setup & Launch         ║"
echo -e " ╚════════════════════════════════════════════╝${N}"
echo ""

# ═══════════════════════════════════════════════════════════════════════════
# DETECT PLATFORM
# ═══════════════════════════════════════════════════════════════════════════
step "Detecting platform"

PLAT=""
WIN=0

UNAME="$(uname -s 2>/dev/null)"
if [[ -n "$MSYSTEM" || "$OSTYPE" == "msys" || "$OSTYPE" == mingw* \
   || "$UNAME" == MINGW* || "$UNAME" == MSYS* || "$UNAME" == CYGWIN* ]]; then
    WIN=1; PLAT=windows
    info "Windows — MSYS2 / $UNAME"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    PLAT=macos
    info "macOS"
elif command -v pacman  &>/dev/null; then PLAT=arch;     info "Linux — Arch (pacman)"
elif command -v apt-get &>/dev/null; then PLAT=debian;   info "Linux — Debian/Ubuntu (apt)"
elif command -v dnf     &>/dev/null; then PLAT=fedora;   info "Linux — Fedora (dnf)"
elif command -v zypper  &>/dev/null; then PLAT=opensuse; info "Linux — openSUSE (zypper)"
else
    die "Unknown platform. Install pacman/apt/dnf/zypper/brew, or run from MSYS2."
fi

# ═══════════════════════════════════════════════════════════════════════════
# HELPER: download a file
# ═══════════════════════════════════════════════════════════════════════════
dl() {
    local url="$1" dest="$2" name="$3"
    [ -f "$dest" ] && { ok "$name already present"; return; }
    info "Downloading $name..."
    if   command -v curl &>/dev/null; then curl -sL "$url" -o "$dest"
    elif command -v wget &>/dev/null; then wget -q  "$url" -O "$dest"
    else warn "curl/wget not found — $name skipped"; return; fi
    [ -f "$dest" ] && ok "$name downloaded" || warn "$name download failed"
}

# ═══════════════════════════════════════════════════════════════════════════
# INSTALL DEPENDENCIES
# ═══════════════════════════════════════════════════════════════════════════
step "Installing dependencies"

case "$PLAT" in
    macos)
        if ! command -v brew &>/dev/null; then
            die "Homebrew not found. Install from https://brew.sh then re-run."
        fi
        brew install glew glfw || warn "brew had errors — continuing"
        ;;
    arch)
        sudo pacman -S --needed --noconfirm \
            base-devel glew mesa glu \
            glfw-x11 ttf-dejavu zenity xdg-utils libserialport 2>/dev/null \
        || sudo pacman -S --needed --noconfirm \
            base-devel glew mesa glu \
            glfw-wayland ttf-dejavu zenity xdg-utils libserialport \
        || warn "pacman had errors — some packages may already be installed"
        ;;
    debian)
        sudo apt-get update -q
        sudo apt-get install -y \
            build-essential libglfw3-dev libglew-dev \
            libglu1-mesa-dev mesa-common-dev \
            fonts-dejavu zenity xdg-utils libserialport-dev \
        || warn "apt had errors — continuing"
        ;;
    fedora)
        sudo dnf install -y \
            gcc-c++ make glfw-devel glew-devel mesa-libGLU-devel \
            dejavu-fonts-common zenity xdg-utils libserialport-devel \
        || warn "dnf had errors — continuing"
        ;;
    opensuse)
        sudo zypper install -y \
            gcc-c++ make libglfw-devel glew-devel glu-devel \
            dejavu-fonts zenity xdg-utils libserialport-devel \
        || warn "zypper had errors — continuing"
        ;;
    windows)
        info "Installing MinGW-w64 toolchain via pacman..."
        pacman -S --needed --noconfirm \
            mingw-w64-x86_64-gcc \
            mingw-w64-x86_64-glfw \
            mingw-w64-x86_64-glew \
            mingw-w64-x86_64-freeglut \
            mingw-w64-x86_64-make \
        || warn "pacman had errors — packages may already be installed"
        ;;
esac

ok "Dependencies done"

# ═══════════════════════════════════════════════════════════════════════════
# PROJECT STRUCTURE
# ═══════════════════════════════════════════════════════════════════════════
step "Setting up project structure"

mkdir -p src

dl "https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h" \
   "src/stb_truetype.h" "stb_truetype.h"
dl "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h" \
   "src/stb_image.h" "stb_image.h"

# ── Font ───────────────────────────────────────────────────────────────────
if [ ! -f default.ttf ]; then
    FONT=""
    if [[ $WIN -eq 1 ]]; then
        for f in \
            /mingw64/share/fonts/DejaVuSansMono.ttf \
            /c/Windows/Fonts/consola.ttf \
            /c/Windows/Fonts/cour.ttf \
            /c/Windows/Fonts/arial.ttf; do
            [ -f "$f" ] && { FONT="$f"; break; }
        done
    elif [[ $PLAT == macos ]]; then
        for f in \
            /Library/Fonts/Arial.ttf \
            /System/Library/Fonts/Supplemental/Arial.ttf \
            /Library/Fonts/DejaVuSansMono.ttf; do
            [ -f "$f" ] && { FONT="$f"; break; }
        done
    else
        for f in \
            /usr/share/fonts/TTF/DejaVuSansMono.ttf \
            /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf \
            /usr/share/fonts/dejavu/DejaVuSansMono.ttf \
            /usr/share/fonts/TTF/DejaVuSans.ttf \
            /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
            /usr/share/fonts/noto/NotoSans-Regular.ttf \
            /usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf; do
            [ -f "$f" ] && { FONT="$f"; break; }
        done
    fi
    if [ -n "$FONT" ]; then
        cp "$FONT" default.ttf
        ok "Font: $(basename "$FONT") → default.ttf"
    else
        warn "No font found — place any .ttf in project root as default.ttf"
    fi
else
    ok "default.ttf already present"
fi

# ── src/main.cpp ───────────────────────────────────────────────────────────
if [ ! -f src/main.cpp ]; then
    cat > src/main.cpp << 'MAIN'
#include "Processing.h"
int main() { Processing::run(); return 0; }
MAIN
    ok "src/main.cpp written"
else
    ok "src/main.cpp already present"
fi

# ═══════════════════════════════════════════════════════════════════════════
# WRITE BUILD SCRIPTS
# ═══════════════════════════════════════════════════════════════════════════
step "Writing build scripts"

# ── Determine linker flags for this platform ──────────────────────────────
if [[ $WIN -eq 1 ]]; then
    LD="-lglfw3 -lglew32 -lopengl32 -lglu32 -lcomdlg32 -lshell32 -lole32 -luuid -mwindows -pthread"
    IDE_OUT="ide.exe"
    SKETCH_EXT=".exe"
elif [[ $PLAT == macos ]]; then
    LD="-lglfw -lGLEW -framework OpenGL -framework Cocoa -framework IOKit -lm"
    IDE_OUT="ide"
    SKETCH_EXT=""
else
    LD="-lglfw -lGLEW -lGL -lGLU -lm -pthread"
    IDE_OUT="ide"
    SKETCH_EXT=""
fi

# ── buildIDE.sh ───────────────────────────────────────────────────────────
cat > buildIDE.sh << BIDE
#!/usr/bin/env bash
# Rebuild the gcc-processing IDE
# Usage: ./buildIDE.sh
set -e
echo "[build] Compiling IDE..."
g++ -std=c++17 \\
    src/Processing.cpp \\
    src/IDE.cpp \\
    src/main.cpp \\
    -o $IDE_OUT \\
    $LD
echo "[build] Done: ./$IDE_OUT"
BIDE
chmod +x buildIDE.sh
ok "buildIDE.sh"

# ── build.sh ─────────────────────────────────────────────────────────────
cat > build.sh << BUILD
#!/usr/bin/env bash
# Build a sketch
# Usage: ./build.sh [path/to/sketch.cpp] [output_name]
#   e.g. ./build.sh src/MySketch.cpp MyApp
set -e
SKETCH="\${1:-src/MySketch.cpp}"
OUT="\${2:-SketchApp}"
echo "[build] \$SKETCH → \$OUT${SKETCH_EXT}"
g++ -std=c++17 \\
    src/Processing.cpp \\
    "\$SKETCH" \\
    src/main.cpp \\
    -o "\${OUT}${SKETCH_EXT}" \\
    $LD
echo "[build] Done: ./\${OUT}${SKETCH_EXT}"
BUILD
chmod +x build.sh
ok "build.sh"

# ── run.sh ────────────────────────────────────────────────────────────────
cat > run.sh << RUN
#!/usr/bin/env bash
# Build a sketch and immediately run it
# Usage: ./run.sh [path/to/sketch.cpp] [output_name]
set -e
bash build.sh "\${1:-src/MySketch.cpp}" "\${2:-SketchApp}"
./"\${2:-SketchApp}${SKETCH_EXT}"
RUN
chmod +x run.sh
ok "run.sh"



# ── .gitignore ────────────────────────────────────────────────────────────
if [ ! -f .gitignore ]; then
    cat > .gitignore << 'GIT'
# Compiled binaries
ide
ide.exe
MyApp
MyApp.exe
SketchApp
SketchApp.exe
*.AppImage
*.AppDir/

# Generated source files (written by setup.sh)
src/main.cpp
src/Sketch_run.cpp
src/stb_truetype.h
src/stb_image.h

# Generated build scripts (written by setup.sh)
buildIDE.sh
buildIDE.bat
build.sh
build.bat
run.sh
run.bat
setup.bat

# Font (copied by setup.sh from system)
default.ttf

# Runtime DLLs (Windows)
*.dll

# Build artifacts
ide_appimage
GIT
    ok ".gitignore"
fi

ok "All build scripts written"

# ═══════════════════════════════════════════════════════════════════════════
# WRITE setup.bat (so Windows users can just double-click it)
# Only written when we're on Linux/macOS building the repo for distribution
# On Windows it already exists / isn't needed
# ═══════════════════════════════════════════════════════════════════════════
if [[ $WIN -eq 0 ]]; then
    cat > setup.bat << 'BAT'
@echo off
setlocal EnableDelayedExpansion
echo.
echo   +================================================+
echo   ^|   gcc-processing  --  Windows Setup           ^|
echo   +================================================+
echo.

:: ── Locate MSYS2 (check common install paths) ───────────────────────────
set MSYS2_PATH=
for %%P in (
    "C:\msys64"
    "C:\msys2"
    "C:\tools\msys64"
    "%USERPROFILE%\msys64"
    "%LOCALAPPDATA%\msys64"
    "C:\gcc-processing\msys64"
) do (
    if exist "%%~P\usr\bin\pacman.exe" (
        if "!MSYS2_PATH!"=="" set "MSYS2_PATH=%%~P"
    )
)

if "!MSYS2_PATH!" NEQ "" goto :have_msys2

:: ── MSYS2 not found — download and install it silently ──────────────────
echo [INFO] MSYS2 not found. Downloading installer...
echo [INFO] This is a ~90MB download and will take a moment.
echo.

:: Use PowerShell to download (available on all Windows 7+)
set INSTALLER=%TEMP%\msys2-installer.exe
powershell -NoProfile -Command ^
    "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; ^
     $ProgressPreference = 'SilentlyContinue'; ^
     Invoke-WebRequest ^
       -Uri 'https://github.com/msys2/msys2-installer/releases/download/2024-01-13/msys2-x86_64-20240113.exe' ^
       -OutFile '%INSTALLER%'"

if not exist "%INSTALLER%" (
    echo [ERR] Download failed. Check your internet connection.
    echo.
    echo  You can install MSYS2 manually from: https://www.msys2.org/
    echo  Then re-run this script.
    pause
    exit /b 1
)

echo [OK]  Installer downloaded. Installing MSYS2 to C:\msys64 ...
echo [INFO] A progress window will appear. Please wait for it to finish.
echo.

:: Run installer silently — installs to C:\msys64 by default
"%INSTALLER%" install --root C:\msys64 --confirm-command --accept-messages
if %errorlevel% NEQ 0 (
    :: Try with the older NSIS-style silent flag
    "%INSTALLER%" /S /D=C:\msys64
)

del /f /q "%INSTALLER%" 2>nul

:: Check again
if exist "C:\msys64\usr\bin\pacman.exe" (
    set "MSYS2_PATH=C:\msys64"
    echo [OK]  MSYS2 installed at C:\msys64
    goto :have_msys2
)

echo [ERR] MSYS2 installation failed or did not complete.
echo  Try installing manually from https://www.msys2.org/
echo  Then re-run this script.
pause
exit /b 1

:have_msys2
echo [OK]  MSYS2 found at: !MSYS2_PATH!
echo.

:: ── Update pacman database first ────────────────────────────────────────
echo [INFO] Updating package database...
"!MSYS2_PATH!\usr\bin\bash.exe" -lc "pacman -Sy --noconfirm" 2>nul
echo.

:: ── Install build dependencies ───────────────────────────────────────────
echo [INFO] Installing build dependencies (gcc, glfw, glew)...
"!MSYS2_PATH!\usr\bin\bash.exe" -lc ^
    "pacman -S --needed --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-glfw mingw-w64-x86_64-glew mingw-w64-x86_64-freeglut mingw-w64-x86_64-make"
if %errorlevel% NEQ 0 (
    echo [WARN] pacman had warnings ^(packages may already be installed^) -- continuing
)
echo [OK]  Packages installed.
echo.

:: ── Download stb headers ─────────────────────────────────────────────────
if not exist "src" mkdir src

if not exist "src\stb_truetype.h" (
    echo [INFO] Downloading stb_truetype.h...
    "!MSYS2_PATH!\usr\bin\curl.exe" -sL ^
        "https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h" ^
        -o src\stb_truetype.h 2>nul
    if exist "src\stb_truetype.h" (echo [OK]  stb_truetype.h) else (echo [WARN] stb_truetype.h download failed)
) else ( echo [OK]  stb_truetype.h already present )

if not exist "src\stb_image.h" (
    echo [INFO] Downloading stb_image.h...
    "!MSYS2_PATH!\usr\bin\curl.exe" -sL ^
        "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h" ^
        -o src\stb_image.h 2>nul
    if exist "src\stb_image.h" (echo [OK]  stb_image.h) else (echo [WARN] stb_image.h download failed)
) else ( echo [OK]  stb_image.h already present )

:: ── Write src/main.cpp ────────────────────────────────────────────────────
if not exist "src\main.cpp" (
    (
        echo #include "Processing.h"
        echo int main^(^) { Processing::run^(^); return 0; }
    ) > src\main.cpp
    echo [OK]  src/main.cpp written
) else ( echo [OK]  src/main.cpp already present )

:: ── Find a font ───────────────────────────────────────────────────────────
if not exist "default.ttf" (
    for %%F in (
        "%WINDIR%\Fonts\consola.ttf"
        "%WINDIR%\Fonts\cour.ttf"
        "%WINDIR%\Fonts\arial.ttf"
        "!MSYS2_PATH!\mingw64\share\fonts\DejaVuSansMono.ttf"
    ) do (
        if exist "%%~F" (
            if not exist "default.ttf" (
                copy "%%~F" default.ttf >nul
                echo [OK]  Font copied: %%~nxF
            )
        )
    )
    if not exist "default.ttf" echo [WARN] No font found -- place any .ttf here as default.ttf
) else ( echo [OK]  default.ttf already present )

:: ── Add MinGW to PATH for this session ────────────────────────────────────
set "MINGW_BIN=!MSYS2_PATH!\mingw64\bin"
set "PATH=!MINGW_BIN!;%PATH%"

:: ── Run the bash setup.sh to write build scripts and build the IDE ────────
echo.
echo [INFO] Running setup.sh to build the IDE...
echo.
"!MSYS2_PATH!\usr\bin\bash.exe" -lc "cd \"$(cygpath -u '%CD%')\" && bash setup.sh"
if %errorlevel% NEQ 0 (
    echo.
    echo [ERR] setup.sh reported an error.
    pause
    exit /b 1
)

:: ── Copy DLLs ─────────────────────────────────────────────────────────────
echo [INFO] Copying runtime DLLs...
for %%D in (libglfw3.dll glew32.dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll) do (
    if exist "!MINGW_BIN!\%%D" (
        copy "!MINGW_BIN!\%%D" . >nul 2>&1
        echo [OK]  %%D
    ) else ( echo [WARN] %%D not found )
)

echo.
echo   +================================================+
echo   ^|   Setup complete!                              ^|
echo   +================================================+
echo.
echo   Run the IDE:   ide.exe   (or double-click in Explorer)
echo   Build sketch:  bash build.sh src/MySketch.cpp
echo   Build IDE:     bash buildIDE.sh
echo.
pause
start "" ide.exe
BAT
    ok "setup.bat written (for Windows users to double-click)"
fi

# ═══════════════════════════════════════════════════════════════════════════
# CHECK SOURCE FILES
# ═══════════════════════════════════════════════════════════════════════════
step "Checking source files"

MISSING=0
for f in src/Processing.h src/Processing.cpp src/IDE.cpp; do
    if [ ! -f "$f" ]; then warn "Missing: $f"; MISSING=$((MISSING+1))
    else ok "Found: $f"; fi
done

if [ $MISSING -gt 0 ]; then
    echo ""
    warn "Copy the missing files into src/ then re-run: bash setup.sh"
    die "Cannot build — missing source files."
fi

# ═══════════════════════════════════════════════════════════════════════════
# BUILD THE IDE
# ═══════════════════════════════════════════════════════════════════════════
step "Building IDE"

bash buildIDE.sh
if [ $? -ne 0 ]; then
    die "Build failed — see errors above."
fi

# ═══════════════════════════════════════════════════════════════════════════
# WINDOWS: collect DLLs
# ═══════════════════════════════════════════════════════════════════════════
if [[ $WIN -eq 1 ]]; then
    step "Collecting runtime DLLs"
    DLL_DIR="/mingw64/bin"
    for dll in libglfw3.dll glew32.dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll; do
        if [ -f "$DLL_DIR/$dll" ]; then cp "$DLL_DIR/$dll" . && ok "$dll"
        else warn "$dll not found"; fi
    done
fi

# ═══════════════════════════════════════════════════════════════════════════
# APPIMAGE (Arch Linux — optional)
# ═══════════════════════════════════════════════════════════════════════════
_package_appimage() {
    APP="gcc-processing"
    APPDIR="$SCRIPT_DIR/${APP}.AppDir"
    info "Building static AppImage binary..."
    g++ -std=c++17 \
        src/Processing.cpp src/IDE.cpp src/main.cpp \
        -o ide_appimage \
        -lglfw -lGLEW -lGL -lGLU -lm -pthread \
        -O2 -static-libgcc -static-libstdc++ \
    || { warn "Static build failed — trying dynamic...";
         g++ -std=c++17 src/Processing.cpp src/IDE.cpp src/main.cpp \
             -o ide_appimage -lglfw -lGLEW -lGL -lGLU -lm -pthread -O2; }

    rm -rf "$APPDIR"
    mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/gcc-processing" \
             "$APPDIR/usr/share/icons/hicolor/256x256/apps"

    cp ide_appimage "$APPDIR/usr/bin/ide"
    for f in default.ttf src/stb_truetype.h src/stb_image.h; do
        [ -f "$f" ] && cp "$f" "$APPDIR/usr/share/gcc-processing/"
    done

    cat > "$APPDIR/AppRun" << 'APPRUN'
#!/usr/bin/env bash
HERE="$(dirname "$(readlink -f "$0")")"
SKETCHDIR="$HOME/gcc-processing"
mkdir -p "$SKETCHDIR/src"
for f in stb_truetype.h stb_image.h; do
    [ -f "$SKETCHDIR/src/$f" ] || cp "$HERE/usr/share/gcc-processing/$f" "$SKETCHDIR/src/$f" 2>/dev/null || true
done
[ -f "$SKETCHDIR/default.ttf" ] || cp "$HERE/usr/share/gcc-processing/default.ttf" "$SKETCHDIR/default.ttf" 2>/dev/null || true
[ -f "$SKETCHDIR/src/main.cpp" ] || printf '#include "Processing.h"\nint main(){Processing::run();return 0;}\n' > "$SKETCHDIR/src/main.cpp"
cd "$SKETCHDIR"
exec "$HERE/usr/bin/ide" "$@"
APPRUN
    chmod +x "$APPDIR/AppRun"

    printf '[Desktop Entry]\nName=gcc-processing IDE\nExec=ide\nIcon=gcc-processing\nType=Application\nCategories=Development;IDE;\n' \
        > "$APPDIR/gcc-processing.desktop"
    touch "$APPDIR/gcc-processing.png"

    if ! command -v appimagetool &>/dev/null; then
        info "Downloading appimagetool..."
        curl -sL https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage \
            -o /tmp/appimagetool && chmod +x /tmp/appimagetool
        APPIMAGETOOL=/tmp/appimagetool
    else
        APPIMAGETOOL=appimagetool
    fi

    ARCH=x86_64 "$APPIMAGETOOL" "$APPDIR" "${APP}-x86_64.AppImage"
    rm -f ide_appimage
    ok "Created: ${APP}-x86_64.AppImage"
    info "Share that file. Users run: chmod +x ${APP}-x86_64.AppImage && ./${APP}-x86_64.AppImage"
}

if [[ $PLAT == arch ]]; then
    echo ""
    ask "Package as portable AppImage? (single file, any Linux) [y/N]"
    read -r -p " > " PACK_ANS
    [[ "$PACK_ANS" =~ ^[Yy]$ ]] && { step "Building AppImage"; _package_appimage; }
fi

# ═══════════════════════════════════════════════════════════════════════════
# DONE
# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo -e "${G} ╔══════════════════════════════════════════╗"
echo -e " ║   Setup complete!                        ║"
echo -e " ╚══════════════════════════════════════════╝${N}"
echo ""
echo -e "  ${C}Ctrl+B${N}         build sketch"
echo -e "  ${C}Ctrl+R${N}         build + run"
echo -e "  ${C}Ctrl+.${N}         stop sketch"
echo -e "  ${C}Ctrl+S / O${N}     save / open"
echo -e "  ${C}Ctrl+Shift+M${N}   serial monitor"
echo -e "  ${C}Ctrl+Shift+L${N}   library manager"
echo -e "  ${C}Ctrl+Shift+V${N}   vim mode"
echo -e "  ${C}Ctrl+= / -${N}     font size"
echo ""

# Launch
if [[ $WIN -eq 1 ]]; then
    read -r -p "  Press Enter to launch IDE... " _
    cmd.exe /c start "" ide.exe 2>/dev/null || ./ide.exe &
else
    exec ./ide
fi

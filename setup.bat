@echo off
setlocal EnableDelayedExpansion
:: =============================================================================
:: gcc-processing -- Windows Setup
:: Double-click this file. It installs MSYS2 if needed, then builds ide.exe.
:: =============================================================================

echo.
echo  +============================================+
echo  ^|   gcc-processing -- Windows Setup         ^|
echo  +============================================+
echo.

:: ---------------------------------------------------------------------------
:: 1. Find or install MSYS2
:: ---------------------------------------------------------------------------
set MSYS2=
for %%P in (
    "C:\msys64"
    "C:\msys2"
    "C:\tools\msys64"
    "%LOCALAPPDATA%\msys64"
    "%USERPROFILE%\msys64"
) do (
    if exist "%%~P\usr\bin\bash.exe" (
        if "!MSYS2!"=="" set "MSYS2=%%~P"
    )
)

if "!MSYS2!" NEQ "" goto :have_msys2

:: --- MSYS2 not found -- download and install it ----------------------------
echo [INFO] MSYS2 not found. Downloading installer (~90 MB)...
echo.
set "INST=%TEMP%\msys2-installer.exe"
powershell -NoProfile -Command ^
    "$p='SilentlyContinue';$ProgressPreference=$p; [Net.ServicePointManager]::SecurityProtocol='Tls12'; Invoke-WebRequest -Uri 'https://github.com/msys2/msys2-installer/releases/download/2024-01-13/msys2-x86_64-20240113.exe' -OutFile '%INST%'"

if not exist "%INST%" (
    echo [ERR] Download failed. Check your connection or install MSYS2 manually from https://www.msys2.org/
    pause & exit /b 1
)

echo [INFO] Installing MSYS2 to C:\msys64 ...
"%INST%" install --root C:\msys64 --confirm-command --accept-messages
if not exist "C:\msys64\usr\bin\bash.exe" (
    :: Try legacy NSIS installer flag
    "%INST%" /S /D=C:\msys64
)
del /f /q "%INST%" 2>nul

if exist "C:\msys64\usr\bin\bash.exe" (
    set "MSYS2=C:\msys64"
    echo [OK]  MSYS2 installed at C:\msys64
    goto :have_msys2
)
echo [ERR] MSYS2 installation failed. Install manually from https://www.msys2.org/
pause & exit /b 1

:have_msys2
echo [OK]  MSYS2: !MSYS2!
set "BASH=!MSYS2!\usr\bin\bash.exe"
set "MINGW_BIN=!MSYS2!\mingw64\bin"
set "PATH=!MINGW_BIN!;!MSYS2!\usr\bin;%PATH%"

:: ---------------------------------------------------------------------------
:: 2. Update pacman db once, then install all build deps
:: ---------------------------------------------------------------------------
echo.
echo [INFO] Updating package database...
"!BASH!" -lc "pacman -Sy --noconfirm 2>&1 | tail -3"

echo [INFO] Installing build tools and GL libraries...
"!BASH!" -lc "pacman -S --needed --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-glfw mingw-w64-x86_64-glew mingw-w64-x86_64-freeglut mingw-w64-x86_64-make mingw-w64-x86_64-pkg-config 2>&1 | grep -v warning"
echo [OK]  Build tools ready.

:: ---------------------------------------------------------------------------
:: 3. Download stb headers
:: ---------------------------------------------------------------------------
echo.
echo [INFO] Downloading stb headers...
if not exist "src" mkdir src

if not exist "src\stb_truetype.h" (
    "!BASH!" -lc "curl -sL https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h -o src/stb_truetype.h && echo [OK]  stb_truetype.h || echo [WARN] stb_truetype.h download failed"
) else ( echo [OK]  stb_truetype.h already present )

if not exist "src\stb_image.h" (
    "!BASH!" -lc "curl -sL https://raw.githubusercontent.com/nothings/stb/master/stb_image.h -o src/stb_image.h && echo [OK]  stb_image.h || echo [WARN] stb_image.h download failed"
) else ( echo [OK]  stb_image.h already present )

:: ---------------------------------------------------------------------------
:: 4. Copy a font
:: ---------------------------------------------------------------------------
if not exist "default.ttf" (
    set "FONT="
    for %%F in (
        "%WINDIR%\Fonts\consola.ttf"
        "%WINDIR%\Fonts\cour.ttf"
        "%WINDIR%\Fonts\arial.ttf"
        "!MSYS2!\mingw64\share\fonts\DejaVuSansMono.ttf"
    ) do (
        if exist "%%~F" if "!FONT!"=="" set "FONT=%%~F"
    )
    if "!FONT!" NEQ "" (
        copy "!FONT!" default.ttf >nul
        echo [OK]  Font copied: !FONT!
    ) else (
        echo [WARN] No font found. Place any .ttf here as default.ttf
    )
) else ( echo [OK]  default.ttf already present )

:: ---------------------------------------------------------------------------
:: 5. Write src\main.cpp
:: ---------------------------------------------------------------------------
if not exist "src\main.cpp" (
    (
        echo #include "Processing.h"
        echo int main^(^) ^{ Processing::run^(^)^; return 0^; ^}
    ) > src\main.cpp
    echo [OK]  src\main.cpp written
) else ( echo [OK]  src\main.cpp already present )

:: ---------------------------------------------------------------------------
:: 6. Check required source files
:: ---------------------------------------------------------------------------
echo.
echo [INFO] Checking source files...
set MISSING=0
for %%F in (src\Processing.h src\Processing.cpp src\Platform.h src\IDE.cpp) do (
    if not exist "%%F" (
        echo [WARN] Missing: %%F
        set /a MISSING+=1
    ) else ( echo [OK]  Found: %%F )
)
if !MISSING! GTR 0 (
    echo.
    echo [ERR] Copy missing files into src\ then double-click setup.bat again.
    pause & exit /b 1
)

:: ---------------------------------------------------------------------------
:: 7. Write build scripts
:: ---------------------------------------------------------------------------
echo.
echo [INFO] Writing build scripts...

(
    echo @echo off
    echo echo [build] Compiling IDE...
    echo "!MINGW_BIN!\g++.exe" -std=c++17 ^
        src\Processing.cpp ^
        src\IDE.cpp ^
        src\Processing_defaults.cpp ^
        src\main.cpp ^
        -o ide.exe ^
        -lglfw3 -lglew32 -lopengl32 -lglu32 ^
        -lcomdlg32 -lshell32 -lole32 -luuid ^
        -pthread -O2 ^
        -D_USE_MATH_DEFINES ^
        -mconsole
    echo if %%ERRORLEVEL%% neq 0 ^( echo [ERR] Build failed. ^& pause ^& exit /b 1 ^)
    echo echo [build] Done: ide.exe
) > buildIDE.bat
echo [OK]  buildIDE.bat

(
    echo @echo off
    echo set SKETCH=%%1
    echo if "%%SKETCH%%"=="" set SKETCH=src\MySketch.cpp
    echo set OUT=%%2
    echo if "%%OUT%%"=="" set OUT=SketchApp
    echo echo [build] %%SKETCH%% -^> %%OUT%%.exe
    echo "!MINGW_BIN!\g++.exe" -std=c++17 ^
        src\Processing.cpp ^
        "%%SKETCH%%" ^
        src\Processing_defaults.cpp ^
        src\main.cpp ^
        -o "%%OUT%%.exe" ^
        -lglfw3 -lglew32 -lopengl32 -lglu32 ^
        -mwindows -pthread -O2 ^
        -D_USE_MATH_DEFINES
    echo if %%ERRORLEVEL%% neq 0 ^( echo [ERR] Build failed. ^& pause ^& exit /b 1 ^)
    echo echo [build] Done: %%OUT%%.exe
) > build.bat
echo [OK]  build.bat

:: Windows: .bat files only (no .sh generated here)

:: ---------------------------------------------------------------------------
:: 8. Build the IDE
:: ---------------------------------------------------------------------------
echo.
echo [INFO] Building IDE...
"!MINGW_BIN!\g++.exe" -std=c++17 ^
    src\Processing.cpp ^
    src\IDE.cpp ^
    src\Processing_defaults.cpp ^
    src\main.cpp ^
    -o ide.exe ^
    -lglfw3 -lglew32 -lopengl32 -lglu32 ^
    -lcomdlg32 -lshell32 -lole32 -luuid ^
    -pthread -O2 ^
    -D_USE_MATH_DEFINES ^
    -mconsole

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERR] Build failed -- see errors above.
    pause & exit /b 1
)
echo [OK]  ide.exe built

:: ---------------------------------------------------------------------------
:: 9. Collect DLLs
:: ---------------------------------------------------------------------------
echo.
echo [INFO] Collecting runtime DLLs...
for %%D in (
    libglfw3.dll glfw3.dll
    glew32.dll libglew32.dll
    libgcc_s_seh-1.dll
    libstdc++-6.dll
    libwinpthread-1.dll
) do (
    if exist "!MINGW_BIN!\%%D" (
        if not exist "%%D" (
            copy "!MINGW_BIN!\%%D" . >nul
            echo [OK]  %%D
        )
    )
)

:: Auto-collect any additional DLLs the binary links against (via bash/objdump)
"!BASH!" -lc "cd '$(cygpath -u "%CD%")' && objdump -p ide.exe 2>/dev/null | grep 'DLL Name' | awk '{print $3}' | while read dll; do [ -f /mingw64/bin/$dll ] && [ ! -f ./$dll ] && cp /mingw64/bin/$dll . && echo "[OK]  $dll (auto)"; done"

:: ---------------------------------------------------------------------------
:: 10. Done
:: ---------------------------------------------------------------------------
echo.
echo  +============================================+
echo  ^|   Setup complete!                         ^|
echo  +============================================+
echo.
echo   Run IDE:       double-click ide.exe
echo   Build sketch:  build.bat MySketch.cpp
echo   Rebuild IDE:   buildIDE.bat
echo.
pause
:: Launch IDE in its own window (console visible for error messages)
start "gcc-processing IDE" ide.exe

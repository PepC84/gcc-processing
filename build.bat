@echo off
set SKETCH=%1
if "%SKETCH%"=="" set SKETCH=src\MySketch.cpp
set OUT=%2
if "%OUT%"=="" set OUT=SketchApp
echo [build] %SKETCH% -> %OUT%.exe
"C:\msys64\mingw64\bin\g++.exe" -std=c++17         src\Processing.cpp         "%SKETCH%"         src\Processing_defaults.cpp         src\main.cpp         -o "%OUT%.exe"         -lglfw3 -lglew32 -lopengl32 -lglu32         -mwindows -pthread -O2         -D_USE_MATH_DEFINES
if %ERRORLEVEL% neq 0 ( echo [ERR] Build failed. & pause & exit /b 1 )
echo [build] Done: %OUT%.exe

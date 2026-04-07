#!/usr/bin/env bash
set -e
bash build.sh "${1:-src/MySketch.cpp}" "${2:-SketchApp}"
./"${2:-SketchApp}"

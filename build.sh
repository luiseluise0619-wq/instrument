#!/usr/bin/env bash
#
# VocalChop Studio - build helper for macOS / Linux.
# Usage:  ./build.sh
#
set -e

echo "================================================"
echo "  VocalChop Studio  -  build helper"
echo "================================================"

if ! command -v cmake >/dev/null 2>&1; then
    echo "[ERROR] CMake not found. Install it: https://cmake.org/download/"
    echo "        (macOS: 'brew install cmake')"
    exit 1
fi
if ! command -v git >/dev/null 2>&1; then
    echo "[ERROR] Git not found. Install it: https://git-scm.com/downloads"
    exit 1
fi

echo "[1/2] Configuring (first run downloads JUCE + Signalsmith)..."
cmake -B build -DCMAKE_BUILD_TYPE=Release

echo "[2/2] Building (this can take several minutes)..."
cmake --build build --config Release

echo
echo "================================================"
echo "  DONE! Plugins are under:"
echo "  build/VocalChopStudio_artefacts/Release/"
echo "================================================"
echo
echo "macOS: the AU/VST3 can be copied to"
echo "  ~/Library/Audio/Plug-Ins/Components/   (AU, for Logic)"
echo "  ~/Library/Audio/Plug-Ins/VST3/         (VST3)"

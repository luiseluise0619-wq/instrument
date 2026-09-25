#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

BUILD_DIR="${1:-build-macos-release}"
command -v cmake >/dev/null || { echo "Install CMake 3.24+ first." >&2; exit 1; }

cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --config Release --parallel "${JOBS:-4}"

ART="$BUILD_DIR/VocalChopStudio_artefacts/Release"
echo "SLYCE macOS build completed from the shared 3.3.5 source."
echo "VST3:      $ART/VST3"
echo "AU:        $ART/AU"
echo "Standalone:$ART/Standalone"
if [ -d "$ART/AAX" ]; then
  echo "AAX:       $ART/AAX (unsigned unless signed separately)"
fi

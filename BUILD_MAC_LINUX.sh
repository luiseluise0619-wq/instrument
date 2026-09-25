#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
build_dir="${1:-build}"
command -v cmake >/dev/null || { echo "Install CMake 3.24+ first." >&2; exit 1; }
cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --config Release --parallel 4
printf 'Build completed: %s/VocalChopStudio_artefacts
' "$build_dir"

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
grep -q 'new SlyceReferenceEditor' Source/PluginProcessor.cpp || { echo 'Wrong source folder'; exit 1; }
cmake -S . -B build-reference-ui -DCMAKE_BUILD_TYPE=Release -DSLYCE_TESTS_ONLY=OFF "$@"
cmake --build build-reference-ui --target VocalChopStudio_Standalone VocalChopStudio_VST3 --parallel 4
printf '\nREF-20260920-04: NEW binaries are under build-reference-ui/VocalChopStudio_artefacts/Release/\n'
printf 'Open the new Standalone first. Back up your existing installed plugin before replacing it.\n'

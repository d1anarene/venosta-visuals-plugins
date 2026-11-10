#!/usr/bin/env bash
# Build the native plugin on macOS. Usage:
#   scripts/build-macos.sh "/path/to/AfterEffectsSDK/Examples" "/path/to/Premiere Pro SDK"
# STATUS: not yet run on a Mac. See docs/INSTALLATION.md.
set -euo pipefail
cd "$(dirname "$0")/.."
[[ "$(uname)" == "Darwin" ]] || { echo "macOS only"; exit 1; }
AE_SDK="${1:?After Effects SDK Examples folder}"
PR_SDK="${2:?Premiere Pro SDK folder}"
./scripts/run-core-tests.sh >/dev/null && echo "core tests: passed"
cmake -S . -B build-mac -G Xcode -DFE_BUILD_PLUGIN=ON -DAE_SDK_DIR="$AE_SDK" -DPR_SDK_DIR="$PR_SDK"
cmake --build build-mac --config Release --target FilmEffects
echo "Built: $(find build-mac -name 'FilmEffects.plugin' -maxdepth 3 | head -1)"

#!/bin/bash
#
#  build.sh - configure and build AB Reference in Release.
#
#  Nothing clever here; it exists so the exact configuration used to produce a
#  build is written down rather than living in somebody's shell history.
#
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

cd "$PROJECT_DIR"

cmake -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR"

echo
echo "Verifying the meters before anything is judged by ear:"
echo
"$BUILD_DIR/MeterCheck_artefacts/Release/MeterCheck"

echo
echo "Rendering the panel so the layout can be checked without a DAW:"
"$BUILD_DIR/EditorShot_artefacts/Release/EditorShot" "$BUILD_DIR/editor.png"

# The second shot is the state the plugin is actually used in - a reference
# loaded, a loop region drawn on it. A waveform that is only ever rendered empty
# is a waveform nobody has looked at.
"$BUILD_DIR/EditorShot_artefacts/Release/EditorShot" "$BUILD_DIR/editor-loaded.png" --demo

echo
echo "Built:"
ls -d "$BUILD_DIR/ABReference_artefacts/Release/"*/*.{vst3,component,app} 2>/dev/null || true

#!/bin/bash
#
#  install.sh - put the built AU and VST3 where the hosts look, and make sure
#  they actually pick up the new build.
#
#  The cache flush is the part that matters. macOS keeps a registry of audio
#  units, and Logic keeps a scan cache of its own on top of that; replacing the
#  .component on disk does not by itself make either of them let go of the old
#  one. Skip this step and the next hour is spent testing a binary from before
#  the fix - a failure mode that looks exactly like the fix not working.
#
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARTEFACTS="$PROJECT_DIR/build/ABReference_artefacts/Release"

AU_SOURCE="$ARTEFACTS/AU/AB Reference.component"
VST3_SOURCE="$ARTEFACTS/VST3/AB Reference.vst3"

AU_DEST="$HOME/Library/Audio/Plug-Ins/Components"
VST3_DEST="$HOME/Library/Audio/Plug-Ins/VST3"

if [[ ! -d "$AU_SOURCE" ]]; then
    echo "No build found at $ARTEFACTS - run Scripts/build.sh first." >&2
    exit 1
fi

if pgrep -xq "Logic Pro"; then
    echo "Logic Pro is running. Quit it first: it holds the old plugin open and" >&2
    echo "will not rescan while it is." >&2
    exit 1
fi

mkdir -p "$AU_DEST" "$VST3_DEST"

rm -rf "$AU_DEST/AB Reference.component" "$VST3_DEST/AB Reference.vst3"
cp -R "$AU_SOURCE"   "$AU_DEST/"
cp -R "$VST3_SOURCE" "$VST3_DEST/"

echo "Installed to $AU_DEST and $VST3_DEST"

# Make the system forget the previous registration.
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo
echo "Validating the Audio Unit:"
auval -v aufx Abrf Tzor

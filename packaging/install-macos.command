#!/bin/bash
set -u
cd "$(dirname "$0")" || { echo "ERROR: cannot find my own folder."; exit 1; }
: "${HOME:?HOME is not set}"
# Per-user first (needs no password); the system-wide paths are scanned too
# because every release before this one told people to install there.
VST3DIRS="$HOME/Library/Audio/Plug-Ins/VST3 /Library/Audio/Plug-Ins/VST3"
AUDIRS="$HOME/Library/Audio/Plug-Ins/Components /Library/Audio/Plug-Ins/Components"
# shellcheck source=/dev/null
. ./slyce-install-common.sh
install_slyce
rc=$?
# Gatekeeper flags anything downloaded; without this a plug-in scan can skip
# the bundle silently instead of reporting why.
if [ $rc -eq 0 ]; then
  xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/VST3/Slyce.vst3" 2>/dev/null
  xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/Components/Slyce.component" 2>/dev/null
fi
read -r -p "Press return to close."
exit $rc

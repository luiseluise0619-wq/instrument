#!/bin/bash
# ---------------------------------------------------------------------
#  Slyce installer - macOS
#
#  Removes any previous install before copying, so nothing from an older
#  build survives inside the new bundle. That includes the pre-3.0 name
#  "VocalChop Studio", which otherwise stays behind and shows up as a
#  second plugin in the DAW.
#
#  Double-click this file in Finder. If macOS refuses to run it:
#  right-click > Open, then Open again.
# ---------------------------------------------------------------------
set -u
cd "$(dirname "$0")"

VST3DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AUDIR="$HOME/Library/Audio/Plug-Ins/Components"

if [ ! -d "Slyce.vst3" ]; then
  echo "ERROR: Slyce.vst3 is not next to this script."
  echo "Unzip the whole download first, then run this from inside that folder."
  read -r -p "Press return to close."
  exit 1
fi

# Installing per-user needs no password and survives OS updates; the
# system-wide /Library path would need sudo for no benefit here.
mkdir -p "$VST3DIR" "$AUDIR"

echo "Removing any previous version..."
rm -rf "$VST3DIR/Slyce.vst3" \
       "$VST3DIR/VocalChop Studio.vst3" \
       "$VST3DIR/VocalChopStudio.vst3" \
       "$AUDIR/Slyce.component" \
       "$AUDIR/VocalChop Studio.component" \
       "$AUDIR/VocalChopStudio.component"

echo "Installing Slyce..."
cp -R "Slyce.vst3" "$VST3DIR/" || { echo "ERROR: copy failed."; read -r; exit 1; }
[ -d "Slyce.component" ] && cp -R "Slyce.component" "$AUDIR/"

# Gatekeeper flags anything downloaded; without this the DAW's plugin scan
# can silently skip the bundle instead of reporting why.
xattr -dr com.apple.quarantine "$VST3DIR/Slyce.vst3" 2>/dev/null
[ -d "$AUDIR/Slyce.component" ] && xattr -dr com.apple.quarantine "$AUDIR/Slyce.component" 2>/dev/null

echo
echo "Done. Installed to:"
echo "  $VST3DIR/Slyce.vst3"
[ -d "$AUDIR/Slyce.component" ] && echo "  $AUDIR/Slyce.component"
echo
echo "Rescan plugins in your DAW. Slyce is an INSTRUMENT, not an effect."
echo
read -r -p "Press return to close."

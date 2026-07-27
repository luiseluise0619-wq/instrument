#!/bin/bash
# ---------------------------------------------------------------------
#  Slyce installer - Linux
#  Removes any previous install (including the pre-3.0 "VocalChop Studio"
#  name) before copying, so the DAW never sees two of them.
# ---------------------------------------------------------------------
set -u
cd "$(dirname "$0")"

VST3DIR="$HOME/.vst3"

if [ ! -d "Slyce.vst3" ]; then
  echo "ERROR: Slyce.vst3 is not next to this script."
  echo "Unzip the whole download first, then run this from inside that folder."
  exit 1
fi

mkdir -p "$VST3DIR"

echo "Removing any previous version..."
rm -rf "$VST3DIR/Slyce.vst3" \
       "$VST3DIR/VocalChop Studio.vst3" \
       "$VST3DIR/VocalChopStudio.vst3"

echo "Installing Slyce..."
cp -R "Slyce.vst3" "$VST3DIR/" || { echo "ERROR: copy failed."; exit 1; }

echo
echo "Done. Installed to $VST3DIR/Slyce.vst3"
echo "Rescan plugins in your DAW. Slyce is an INSTRUMENT, not an effect."

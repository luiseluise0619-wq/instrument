#!/bin/bash
# ---------------------------------------------------------------------
#  Slyce installer - Linux
#
#  Removes ONLY Slyce's own bundles, by exact name, and nothing else.
#  The list is printed before anything is touched so you can see it.
# ---------------------------------------------------------------------
set -u
cd "$(dirname "$0")" || { echo "ERROR: cannot find my own folder."; exit 1; }

: "${HOME:?HOME is not set - cannot find your plug-in folder}"
VST3DIR="$HOME/.vst3"

# Every name Slyce has ever shipped under. Exact names only - no wildcards,
# so there is no way for this to match another vendor's plugin.
NAMES=("Slyce.vst3" "VocalChop Studio.vst3" "VocalChopStudio.vst3")

if [ ! -d "Slyce.vst3" ]; then
  echo "ERROR: Slyce.vst3 is not next to this script."
  echo "Unzip the whole download first, then run this from inside that folder."
  exit 1
fi

found=()
for n in "${NAMES[@]}"; do [ -e "$VST3DIR/$n" ] && found+=("$VST3DIR/$n"); done

if [ ${#found[@]} -eq 0 ]; then
  echo "No previous version found. Installing fresh."
else
  echo "These previous Slyce files will be replaced:"
  for f in "${found[@]}"; do echo "   $f"; done
  echo "Nothing else in $VST3DIR is touched."
  echo
  for f in "${found[@]}"; do rm -rf "$f"; done
fi

mkdir -p "$VST3DIR"
echo "Installing Slyce..."
cp -R "Slyce.vst3" "$VST3DIR/" || { echo "ERROR: copy failed."; exit 1; }

echo
echo "Done. Installed to $VST3DIR/Slyce.vst3"
echo "Rescan plugins in your DAW. Slyce is an INSTRUMENT, not an effect."

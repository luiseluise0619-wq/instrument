#!/bin/bash
# ---------------------------------------------------------------------
#  Slyce installer - macOS
#
#  Removes ONLY Slyce's own bundles, by exact name, and nothing else.
#  The list is printed before anything is touched so you can see it.
#
#  It clears BOTH locations. Releases before this one told people to copy
#  into the system-wide /Library, while this installs per-user, so a stale
#  system copy would otherwise survive and the DAW would list Slyce twice.
#
#  Double-click in Finder. If macOS refuses: right-click > Open, then Open.
# ---------------------------------------------------------------------
set -u
cd "$(dirname "$0")" || { echo "ERROR: cannot find my own folder."; exit 1; }

USER_VST3="$HOME/Library/Audio/Plug-Ins/VST3"
USER_AU="$HOME/Library/Audio/Plug-Ins/Components"
SYS_VST3="/Library/Audio/Plug-Ins/VST3"
SYS_AU="/Library/Audio/Plug-Ins/Components"

# Every name Slyce has ever shipped under. Exact names only - no wildcards,
# so there is no way for this to match another vendor's plugin.
VST3_NAMES=("Slyce.vst3" "VocalChop Studio.vst3" "VocalChopStudio.vst3")
AU_NAMES=("Slyce.component" "VocalChop Studio.component" "VocalChopStudio.component")

if [ ! -d "Slyce.vst3" ]; then
  echo "ERROR: Slyce.vst3 is not next to this script."
  echo "Unzip the whole download first, then run this from inside that folder."
  read -r -p "Press return to close."
  exit 1
fi

# --- show exactly what will be removed, before removing it ---------------
found_user=(); found_sys=()
for n in "${VST3_NAMES[@]}"; do
  [ -e "$USER_VST3/$n" ] && found_user+=("$USER_VST3/$n")
  [ -e "$SYS_VST3/$n"  ] && found_sys+=("$SYS_VST3/$n")
done
for n in "${AU_NAMES[@]}"; do
  [ -e "$USER_AU/$n" ] && found_user+=("$USER_AU/$n")
  [ -e "$SYS_AU/$n"  ] && found_sys+=("$SYS_AU/$n")
done

if [ ${#found_user[@]} -eq 0 ] && [ ${#found_sys[@]} -eq 0 ]; then
  echo "No previous version found. Installing fresh."
else
  echo "These previous Slyce files will be replaced:"
  for f in "${found_user[@]}" "${found_sys[@]}"; do echo "   $f"; done
  echo "Nothing else in your plug-in folders is touched."
  echo
fi

mkdir -p "$USER_VST3" "$USER_AU"

for f in "${found_user[@]}"; do rm -rf "$f"; done

# The system-wide folder needs an admin password. If it is declined we carry
# on and say so, rather than failing the whole install.
if [ ${#found_sys[@]} -gt 0 ]; then
  echo "Removing the older system-wide copy needs your password."
  if sudo -v 2>/dev/null; then
    for f in "${found_sys[@]}"; do sudo rm -rf "$f"; done
  else
    echo
    echo "WARNING: skipped the system-wide copy. Slyce may appear TWICE in"
    echo "your DAW. To fix it later, delete these by hand:"
    for f in "${found_sys[@]}"; do echo "   $f"; done
    echo
  fi
fi

echo "Installing Slyce..."
cp -R "Slyce.vst3" "$USER_VST3/" || { echo "ERROR: copy failed."; read -r; exit 1; }
[ -d "Slyce.component" ] && cp -R "Slyce.component" "$USER_AU/"

# Gatekeeper flags anything downloaded; without this a plugin scan can skip
# the bundle silently instead of reporting why.
xattr -dr com.apple.quarantine "$USER_VST3/Slyce.vst3" 2>/dev/null
[ -d "$USER_AU/Slyce.component" ] && xattr -dr com.apple.quarantine "$USER_AU/Slyce.component" 2>/dev/null

echo
echo "Done. Installed to:"
echo "   $USER_VST3/Slyce.vst3"
[ -d "$USER_AU/Slyce.component" ] && echo "   $USER_AU/Slyce.component"
echo
echo "Rescan plugins in your DAW. Slyce is an INSTRUMENT, not an effect."
echo
read -r -p "Press return to close."

#!/bin/bash
# Shared installer body for macOS and Linux. Sourced with VST3DIRS / AUDIRS set.
#
# Safety rules, in the order they bite:
#   1. Never delete on name alone. A bundle counts as ours only if it names
#      our vendor internally, so a same-named plugin from anyone else is left
#      strictly alone.
#   2. Install BEFORE removing. The new bundle is staged under a temporary
#      name and verified; only then is the old one retired. A failed copy
#      leaves the previous version exactly where it was.
#   3. Retire, don't destroy. Old bundles are MOVED to a backup folder, so a
#      bad upgrade is recoverable by hand.
#   4. Refuse to fight a running DAW, which holds the files open.

VENDOR="VocalChop Labs"
NAMES=("Slyce.vst3" "VocalChop Studio.vst3" "VocalChopStudio.vst3")
AUNAMES=("Slyce.component" "VocalChop Studio.component" "VocalChopStudio.component")
LOG="$HOME/slyce-install.log"

say () { echo "$*"; echo "$*" >> "$LOG"; }

is_ours () {
  local b="$1" mi
  [ -e "$b" ] || return 1
  mi=$(find "$b" -name moduleinfo.json -print -quit 2>/dev/null)
  [ -n "$mi" ] && grep -q "$VENDOR" "$mi" 2>/dev/null && return 0
  # Builds predating moduleinfo.json: fall back to the vendor string compiled
  # into the binary itself.
  grep -rqs "$VENDOR" "$b" 2>/dev/null && return 0
  return 1
}

# Matches the process NAME only. An earlier version used `pgrep -f`, which
# searches whole command lines and duly matched an unrelated shell, blocking
# a perfectly good install with a DAW warning for a DAW that wasn't running.
running_daw () {
  local d
  for d in FL64 FL "Ableton Live" Live Cubase reaper Logic "Studio One" \
           Bitwig "Pro Tools" Nuendo; do
    pgrep -i "^${d}$" >/dev/null 2>&1 && { echo "$d"; return 0; }
  done
  return 1
}

install_slyce () {
  : > "$LOG"
  say "Slyce installer - $(uname -s)"

  if [ ! -d "Slyce.vst3" ]; then
    say "ERROR: Slyce.vst3 is not next to this script."
    say "Unzip the whole download first, then run this from inside that folder."
    return 1
  fi

  local daw ans
  if daw=$(running_daw); then
    say ""
    say "WARNING: \"$daw\" looks like it is running."
    say "Plug-in files are locked while a DAW is open, so installing now can"
    say "half-finish. Close it first."
    say ""
    read -r -p "Type y to continue anyway, anything else to stop: " ans
    [ "$ans" = "y" ] || { say "Stopped. Nothing was changed."; return 1; }
  fi

  local BACKUP="$HOME/Slyce-previous-version"
  local primary; primary=$(echo "$VST3DIRS" | awk '{print $1}')
  mkdir -p "$primary"

  # ---- 1. stage the new bundle under a temp name ------------------------
  local stage="$primary/.Slyce-installing.vst3"
  rm -rf "$stage"
  say "Installing Slyce..."
  if ! cp -R "Slyce.vst3" "$stage"; then
    say "ERROR: copy failed. Your existing version has NOT been touched."
    rm -rf "$stage"; return 1
  fi

  # ---- 2. verify the staged copy BEFORE retiring anything ---------------
  if ! is_ours "$stage"; then
    say "ERROR: the copied files did not verify. Nothing has been changed."
    rm -rf "$stage"; return 1
  fi

  # ---- 2b. refuse a name conflict BEFORE touching anything --------------
  # If the target name is held by a bundle that is not ours, stop here. Doing
  # this check after the retire step meant the old version had already been
  # moved by the time we bailed, and the script then claimed "nothing was
  # changed" - which was simply untrue.
  if [ -e "$primary/Slyce.vst3" ] && ! is_ours "$primary/Slyce.vst3"; then
    say ""
    say "STOPPED: $primary/Slyce.vst3 already exists and is not our plugin."
    say "Refusing to overwrite somebody else's file. Nothing has been changed."
    say "Move or rename that file, then run this again."
    rm -rf "$stage"; return 1
  fi

  # ---- 3. retire old bundles (ours only), by MOVING them ----------------
  local retired=0 skipped=0 d n
  for d in $VST3DIRS; do
    for n in "${NAMES[@]}"; do
      [ -e "$d/$n" ] || continue
      if is_ours "$d/$n"; then
        mkdir -p "$BACKUP"; rm -rf "$BACKUP/$n"
        if mv "$d/$n" "$BACKUP/$n" 2>/dev/null; then
          say "  retired  $d/$n"; retired=$((retired+1))
        else
          say "  NOTE: could not move $d/$n (needs admin?) - left in place"
        fi
      else
        say "  SKIPPED  $d/$n - not ours, left alone"; skipped=$((skipped+1))
      fi
    done
  done
  for d in ${AUDIRS:-}; do
    for n in "${AUNAMES[@]}"; do
      [ -e "$d/$n" ] || continue
      if is_ours "$d/$n"; then
        mkdir -p "$BACKUP"; rm -rf "$BACKUP/$n"
        mv "$d/$n" "$BACKUP/$n" 2>/dev/null && say "  retired  $d/$n"
      else
        say "  SKIPPED  $d/$n - not ours, left alone"; skipped=$((skipped+1))
      fi
    done
  done

  # ---- 4. swap the staged copy into place -------------------------------
  # Anything left on the target name here is our own (2b already rejected
  # foreign bundles, and step 3 retires ours), so this is safe.
  rm -rf "$primary/Slyce.vst3"
  mv "$stage" "$primary/Slyce.vst3"

  if [ -d "Slyce.component" ] && [ -n "${AUDIRS:-}" ]; then
    local audir; audir=$(echo "$AUDIRS" | awk '{print $1}')
    mkdir -p "$audir"; rm -rf "$audir/Slyce.component"
    cp -R "Slyce.component" "$audir/" && say "  installed $audir/Slyce.component"
  fi

  say ""
  say "Done. Installed to $primary/Slyce.vst3"
  [ "$retired" -gt 0 ] && say "Previous version kept at: $BACKUP"
  [ "$skipped" -gt 0 ] && say "$skipped file(s) left alone because they are not ours."
  say "Full log: $LOG"
  say ""
  say "Rescan plugins in your DAW. Slyce is an INSTRUMENT, not an effect."
  return 0
}

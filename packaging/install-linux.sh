#!/bin/bash
set -u
cd "$(dirname "$0")" || { echo "ERROR: cannot find my own folder."; exit 1; }
: "${HOME:?HOME is not set - cannot find your plug-in folder}"
VST3DIRS="$HOME/.vst3 /usr/lib/vst3 /usr/local/lib/vst3"
AUDIRS=""
# shellcheck source=/dev/null
. ./slyce-install-common.sh
install_slyce

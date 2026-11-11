#!/usr/bin/env bash
# Copy a built FilmEffects.plugin into Adobe's shared MediaCore plug-in folder (needs admin rights).
# Only touches FilmEffects.plugin; any previous copy is moved to a timestamped backup first.
set -euo pipefail
cd "$(dirname "$0")/.."
SRC="${1:-$(find build-mac -name 'FilmEffects.plugin' -maxdepth 3 | head -1)}"
# Documented common plug-in location (Premiere Pro SDK guide, "Plugin Installation", 22.0+).
DEST="/Library/Application Support/Adobe/Common/Plugins/7.0/MediaCore"
ALT="/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore"
if [[ -d "$ALT/FilmEffects.plugin" ]]; then
  echo "Note: an older FilmEffects.plugin exists in $ALT - remove it to avoid loading two copies."
fi
[[ -d "$SRC" ]] || { echo "FilmEffects.plugin not found; build first"; exit 1; }
sudo mkdir -p "$DEST"
if [[ -d "$DEST/FilmEffects.plugin" ]]; then
  sudo mv "$DEST/FilmEffects.plugin" "$DEST/FilmEffects.plugin.backup-$(date +%Y%m%d%H%M%S)"
fi
sudo cp -R "$SRC" "$DEST/"
echo "Installed to $DEST. Restart Premiere Pro, then search 'Film Effects' in the Effects panel."

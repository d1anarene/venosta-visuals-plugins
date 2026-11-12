#!/usr/bin/env bash
# Copy the built FilmEffects.plugin into Adobe's shared MediaCore plug-in folder (needs admin
# rights). Any previous copy is moved to a timestamped backup first.
set -euo pipefail
cd "$(dirname "$0")/.."
# Confirmed in Premiere Pro 27.0 Plugin Loading.log on macOS.
DEST="/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore"
ALT="/Library/Application Support/Adobe/Common/Plugins/7.0/MediaCore"
NAMES=("$@")
[[ ${#NAMES[@]} -gt 0 ]] || NAMES=(FilmEffects)
sudo mkdir -p "$DEST"
for name in "${NAMES[@]}"; do
  SRC="$(find build-mac -name "$name.plugin" -maxdepth 3 | head -1)"
  [[ -d "$SRC" ]] || { echo "$name.plugin not found; build first"; exit 1; }
  if [[ -d "$ALT/$name.plugin" ]]; then
    echo "Note: an older $name.plugin exists in $ALT (legacy incorrect folder)."
  fi
  if [[ -d "$DEST/$name.plugin" ]]; then
    sudo mv "$DEST/$name.plugin" "$DEST/$name.plugin.backup-$(date +%Y%m%d%H%M%S)"
  fi
  sudo cp -R "$SRC" "$DEST/"
  echo "Installed $name.plugin"
done
echo "Restart Premiere Pro, then look in Effects > Video Effects > Venosta Visuals."

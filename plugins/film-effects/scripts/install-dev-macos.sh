#!/usr/bin/env bash
# Copy built Film*.plugin bundles (Film Effects, Film Bloom, Film Halation, Film Grain) into Adobe's
# shared MediaCore plug-in folder (needs admin rights). Only touches these four bundles; any
# previous copy is moved to a timestamped backup first.
#   scripts/install-dev-macos.sh            # install all four
#   scripts/install-dev-macos.sh FilmGrain  # install only some
set -euo pipefail
cd "$(dirname "$0")/.."
# Documented common plug-in location (Premiere Pro SDK guide, "Plugin Installation", 22.0+).
DEST="/Library/Application Support/Adobe/Common/Plugins/7.0/MediaCore"
ALT="/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore"
NAMES=("$@")
[[ ${#NAMES[@]} -gt 0 ]] || NAMES=(FilmEffects FilmBloom FilmHalation FilmGrain)
sudo mkdir -p "$DEST"
for name in "${NAMES[@]}"; do
  SRC="$(find build-mac -name "$name.plugin" -maxdepth 3 | head -1)"
  [[ -d "$SRC" ]] || { echo "$name.plugin not found; build first"; exit 1; }
  if [[ -d "$ALT/$name.plugin" ]]; then
    echo "Note: an older $name.plugin exists in $ALT - remove it to avoid loading two copies."
  fi
  if [[ -d "$DEST/$name.plugin" ]]; then
    sudo mv "$DEST/$name.plugin" "$DEST/$name.plugin.backup-$(date +%Y%m%d%H%M%S)"
  fi
  sudo cp -R "$SRC" "$DEST/"
  echo "Installed $name.plugin"
done
echo "Restart Premiere Pro, then look in Effects > Video Effects > Venosta Visuals."

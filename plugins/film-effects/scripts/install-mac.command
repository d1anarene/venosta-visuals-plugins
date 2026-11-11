#!/bin/bash
# Film Effects installer for macOS. Double-click this file (it sits next to the .plugin bundles).
# Copies FilmEffects, FilmBloom, FilmHalation and FilmGrain into Adobe's shared plug-in folder,
# backs up older copies, and clears the download quarantine flag (unsigned plugins that still
# carry it are blocked by macOS). Needs your Mac password once.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
DEST="/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore"
echo "Film Effects installer"
echo "Destination: $DEST"
shopt -s nullglob
PLUGINS=("$HERE"/Film*.plugin)
if [ ${#PLUGINS[@]} -eq 0 ]; then echo "No Film*.plugin found next to this script."; read -r -p "Press Enter to close"; exit 1; fi
sudo mkdir -p "$DEST"
for p in "${PLUGINS[@]}"; do
  name="$(basename "$p")"
  if [ -d "$DEST/$name" ]; then
    sudo mv "$DEST/$name" "$DEST/$name.backup-$(date +%Y%m%d%H%M%S)"
  fi
  sudo cp -R "$p" "$DEST/"
  sudo xattr -dr com.apple.quarantine "$DEST/$name" 2>/dev/null || true
  echo "Installed $name"
done
echo
echo "Done. Restart Premiere Pro, then look in Effects > Video Effects > Venosta Visuals."
read -r -p "Press Enter to close"

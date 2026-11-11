#!/usr/bin/env bash
# Package the built panels as .ccx installers (double-click to install with Creative Cloud Desktop).
# A .ccx is a ZIP with manifest.json at its root (Adobe UXP docs, "Package a UXP plugin").
#   npm run build && ./scripts/package-ccx.sh [output-dir] [version-suffix]
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="$(mkdir -p "${1:-dist/installers}" && cd "${1:-dist/installers}" && pwd)"
SUFFIX="${2:-}"
pack() {  # <dist folder> <file name>
  [[ -f "dist/$1/manifest.json" ]] || { echo "dist/$1 missing - run npm run build"; exit 1; }
  rm -f "$OUT/$2"
  (cd "dist/$1" && zip -qrX "$OUT/$2" .)
  echo "$OUT/$2"
}
pack premiere-panel "TrackRangeExporter-Premiere${SUFFIX}.ccx"
pack ame-companion "TrackRangeExporter-MediaEncoder${SUFFIX}.ccx"

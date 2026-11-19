#!/usr/bin/env bash
# Packages dist/panel as a .ccx (a ZIP with manifest.json at its root; double-click installs it
# with Creative Cloud Desktop).   npm run build && ./scripts/package-ccx.sh [output-dir]
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="$(mkdir -p "${1:-dist/installers}" && cd "${1:-dist/installers}" && pwd)"
[[ -f dist/panel/manifest.json ]] || { echo "dist/panel missing - run npm run build"; exit 1; }
rm -f "$OUT/ConductorTest-Premiere.ccx"
(cd dist/panel && zip -qrX "$OUT/ConductorTest-Premiere.ccx" .)
echo "$OUT/ConductorTest-Premiere.ccx"

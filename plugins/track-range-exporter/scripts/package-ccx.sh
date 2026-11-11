#!/usr/bin/env bash
# Package the built panels as .ccx installers (double-click to install with Creative Cloud Desktop).
# A .ccx is a ZIP with manifest.json at its root (Adobe UXP docs, "Package a UXP plugin").
#   npm run build && ./scripts/package-ccx.sh [output-dir] [version-suffix] [plugin-version]
# plugin-version (e.g. 0.1.7) is written into manifest.json so Creative Cloud sees an update.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="$(mkdir -p "${1:-dist/installers}" && cd "${1:-dist/installers}" && pwd)"
SUFFIX="${2:-}"
PLUGIN_VERSION="${3:-}"
pack() {  # <dist folder> <file name>
  [[ -f "dist/$1/manifest.json" ]] || { echo "dist/$1 missing - run npm run build"; exit 1; }
  local tmp
  tmp="$(mktemp -d)"
  cp -R "dist/$1/." "$tmp/"
  if [[ -n "$PLUGIN_VERSION" ]]; then
    node -e 'const f=process.argv[1],v=process.argv[2];const fs=require("fs");const m=JSON.parse(fs.readFileSync(f,"utf8"));m.version=v;fs.writeFileSync(f,JSON.stringify(m,null,2)+"\n")' \
      "$tmp/manifest.json" "$PLUGIN_VERSION"
  fi
  rm -f "$OUT/$2"
  (cd "$tmp" && zip -qrX "$OUT/$2" .)
  rm -rf "$tmp"
  echo "$OUT/$2"
}
pack premiere-panel "TrackRangeExporter-Premiere${SUFFIX}.ccx"
pack ame-companion "TrackRangeExporter-MediaEncoder${SUFFIX}.ccx"

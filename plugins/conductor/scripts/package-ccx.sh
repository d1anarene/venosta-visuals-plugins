#!/usr/bin/env bash
# Packages dist/panel as Conductor-Premiere.ccx (ZIP with manifest.json at its root).
#   npm run build && ./scripts/package-ccx.sh [output-dir] [file-suffix] [plugin-version]
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="$(mkdir -p "${1:-dist/installers}" && cd "${1:-dist/installers}" && pwd)"
SUFFIX="${2:-}"
VERSION="${3:-}"
[[ -f dist/panel/manifest.json ]] || { echo "dist/panel missing - run npm run build"; exit 1; }
tmp="$(mktemp -d)"
cp -R dist/panel/. "$tmp/"
if [[ -n "$VERSION" ]]; then
  node -e 'const f=process.argv[1],v=process.argv[2];const fs=require("fs");const m=JSON.parse(fs.readFileSync(f,"utf8"));m.version=v;fs.writeFileSync(f,JSON.stringify(m,null,2)+"\n")' "$tmp/manifest.json" "$VERSION"
fi
rm -f "$OUT/Conductor-Premiere$SUFFIX.ccx"
(cd "$tmp" && zip -qrX "$OUT/Conductor-Premiere$SUFFIX.ccx" .)
rm -rf "$tmp"
echo "$OUT/Conductor-Premiere$SUFFIX.ccx"

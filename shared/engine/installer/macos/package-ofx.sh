#!/usr/bin/env bash
# Package an OpenFX bundle (DaVinci Resolve) as a macOS installer that installs
# /Library/OFX/Plugins/<Name>.ofx.bundle. Ad hoc signed, not notarized.
# Usage: package-ofx.sh <Name.ofx.bundle> <output.pkg> <version> <pkg-identifier> [--metal]
set -euo pipefail
[[ $# -ge 4 ]] || { echo "Usage: $0 <Name.ofx.bundle> <output.pkg> <version> <pkg-identifier> [--metal]" >&2; exit 1; }
SOURCE="$(cd "$1" && pwd)"
mkdir -p "$(dirname "$2")"
OUTPUT="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
VERSION="$3"
PKGID="$4"
METAL=0
[[ "${5:-}" == "--metal" ]] && METAL=1
NAME="$(basename "$SOURCE" .ofx.bundle)"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
BUNDLE="$STAGE/root/Library/OFX/Plugins/$NAME.ofx.bundle"
mkdir -p "$(dirname "$BUNDLE")"
ditto "$SOURCE" "$BUNDLE"
BIN="$BUNDLE/Contents/MacOS/$NAME.ofx"
[[ -f "$BIN" ]] || { echo "Missing $BIN" >&2; exit 1; }
chmod 755 "$BIN"
if [[ $METAL -eq 1 ]]; then
  ls "$BUNDLE/Contents/Resources/"*.metallib >/dev/null || { echo "Missing Metal library (GPU path)" >&2; exit 1; }
fi
architectures=" $(lipo -archs "$BIN") "
[[ "$architectures" == *" arm64 "* && "$architectures" == *" x86_64 "* ]] || { echo "Bundle must contain arm64 and x86_64" >&2; exit 1; }
codesign --force --sign - --timestamp=none "$BUNDLE"
codesign --verify --strict --all-architectures "$BUNDLE"
pkgbuild --analyze --root "$STAGE/root" "$STAGE/components.plist"
python3 - "$STAGE/components.plist" <<'PY'
import pathlib, plistlib, sys
path = pathlib.Path(sys.argv[1])
components = plistlib.loads(path.read_bytes())
for component in components:
    component['BundleIsRelocatable'] = False
    component['BundleIsVersionChecked'] = False
path.write_bytes(plistlib.dumps(components))
PY
pkgbuild --root "$STAGE/root" --component-plist "$STAGE/components.plist" \
  --identifier "$PKGID" --version "$VERSION" \
  --install-location / --ownership recommended "$OUTPUT"

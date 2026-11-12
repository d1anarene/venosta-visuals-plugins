#!/usr/bin/env bash
# Package prebuilt native bundles; does not install anything or include Adobe SDK files.
# Usage: package-macos.sh <bundle-directory> <output.pkg> <package-version>
set -euo pipefail
[[ $# -eq 3 ]] || { echo "Usage: $0 <bundle-directory> <output.pkg> <version>" >&2; exit 1; }
SOURCE="$(cd "$1" && pwd)"
mkdir -p "$(dirname "$2")"
OUTPUT="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
VERSION="$3"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
RELATIVE="Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore"
mkdir -p "$STAGE/root/$RELATIVE" "$STAGE/scripts"
for name in FilmEffects; do
  bundle="$STAGE/root/$RELATIVE/$name.plugin"
  [[ -f "$SOURCE/$name.plugin/Contents/MacOS/$name" ]] || { echo "Missing $name executable" >&2; exit 1; }
  ditto "$SOURCE/$name.plugin" "$bundle"
  chmod 755 "$bundle/Contents/MacOS/$name"
  # Ad hoc signatures support local loading, but are not Developer ID signing/notarization.
  codesign --force --sign - --timestamp=none "$bundle"
  codesign --verify --strict --all-architectures "$bundle"
  architectures=" $(lipo -archs "$bundle/Contents/MacOS/$name") "
  [[ "$architectures" == *" arm64 "* && "$architectures" == *" x86_64 "* ]] || {
    echo "$name must contain both arm64 and x86_64" >&2; exit 1;
  }
  [[ -s "$bundle/Contents/Resources/FilmEffects.metallib" ]]
  xcrun DeRez -useDF -only PiPL "$bundle/Contents/Resources/$name.rsrc" >/dev/null
done
cat > "$STAGE/scripts/preinstall" <<'SCRIPT'
#!/bin/bash
set -euo pipefail
if [ "$(/usr/bin/sw_vers -productVersion | /usr/bin/cut -d. -f1)" -lt 26 ]; then
  echo "Film Effects requires macOS 26 or later." >&2
  exit 1
fi
if /usr/bin/pgrep -f '/Adobe Premiere Pro[^/]*/Adobe Premiere Pro[^/]*.app/Contents/MacOS/' >/dev/null; then
  echo "Quit Adobe Premiere Pro before installing Film Effects." >&2
  exit 1
fi
VOLUME="${3:-/}"
DEST="${VOLUME%/}/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore"
BACKUP="${VOLUME%/}/Library/Application Support/Venosta Visuals/Backups/FilmEffects-$(date +%Y%m%d-%H%M%S)-$$"
for NAME in FilmEffects FilmBloom FilmHalation FilmGrain; do
  if [ -e "$DEST/$NAME.plugin" ]; then
    /bin/mkdir -p "$BACKUP"
    /usr/bin/ditto "$DEST/$NAME.plugin" "$BACKUP/$NAME.plugin"
  fi
done
# Film Bloom / Film Halation / Film Grain were standalone effects until v1.0.11; they are part of
# Film Effects now. Remove them (backed up above).
for NAME in FilmBloom FilmHalation FilmGrain; do
  /bin/rm -rf "$DEST/$NAME.plugin"
done
SCRIPT
chmod 755 "$STAGE/scripts/preinstall"
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
  --scripts "$STAGE/scripts" --identifier com.venostavisuals.filmeffects.preview \
  --version "$VERSION" --install-location / --ownership recommended "$OUTPUT"

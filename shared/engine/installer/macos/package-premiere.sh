#!/usr/bin/env bash
# Package a Premiere Pro effect bundle (<Name>.plugin) as a macOS installer (.pkg) that installs
# into /Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore. Ad hoc signed bundle,
# unsigned / not notarized package. Existing copies are backed up first; --retire removes bundles
# of effects that older versions installed and this version replaces.
# Usage: package-premiere.sh <bundle-dir> <out.pkg> <version> <pkg-identifier> <Name> [--metal]
#          [--min-macos <major>] [--retire Name...]
set -euo pipefail
[[ $# -ge 5 ]] || { echo "Usage: $0 <bundle-dir> <out.pkg> <version> <pkg-identifier> <Name> [--metal] [--retire Name...]" >&2; exit 1; }
SOURCE="$(cd "$1" && pwd)"
mkdir -p "$(dirname "$2")"
OUTPUT="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
VERSION="$3"; PKGID="$4"; NAME="$5"; shift 5
METAL=0; RETIRE=(); MINMAC=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --metal) METAL=1 ;;
    --min-macos) shift; MINMAC="$1" ;;
    --retire) shift; RETIRE+=("$1") ;;
    *) echo "unknown option $1" >&2; exit 1 ;;
  esac
  shift
done
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
RELATIVE="Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore"
mkdir -p "$STAGE/root/$RELATIVE" "$STAGE/scripts"
bundle="$STAGE/root/$RELATIVE/$NAME.plugin"
[[ -f "$SOURCE/$NAME.plugin/Contents/MacOS/$NAME" ]] || { echo "Missing $NAME executable" >&2; exit 1; }
ditto "$SOURCE/$NAME.plugin" "$bundle"
chmod 755 "$bundle/Contents/MacOS/$NAME"
# Ad hoc signatures support local loading, but are not Developer ID signing/notarization.
codesign --force --sign - --timestamp=none "$bundle"
codesign --verify --strict --all-architectures "$bundle"
architectures=" $(lipo -archs "$bundle/Contents/MacOS/$NAME") "
[[ "$architectures" == *" arm64 "* && "$architectures" == *" x86_64 "* ]] || { echo "$NAME must contain both arm64 and x86_64" >&2; exit 1; }
if [[ $METAL -eq 1 ]]; then
  ls "$bundle/Contents/Resources/"*.metallib >/dev/null || { echo "Missing Metal library" >&2; exit 1; }
fi
xcrun DeRez -useDF -only PiPL "$bundle/Contents/Resources/$NAME.rsrc" >/dev/null
{
  cat <<'SCRIPT'
#!/bin/bash
set -euo pipefail
SCRIPT
  if [[ $MINMAC -gt 0 ]]; then
    echo "if [ \"\$(/usr/bin/sw_vers -productVersion | /usr/bin/cut -d. -f1)\" -lt $MINMAC ]; then"
    echo "  echo \"$NAME requires macOS $MINMAC or later.\" >&2"
    echo "  exit 1"
    echo "fi"
  fi
  cat <<'SCRIPT'
if /usr/bin/pgrep -f '/Adobe Premiere Pro[^/]*/Adobe Premiere Pro[^/]*.app/Contents/MacOS/' >/dev/null; then
  echo "Quit Adobe Premiere Pro before installing." >&2
  exit 1
fi
VOLUME="${3:-/}"
DEST="${VOLUME%/}/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore"
SCRIPT
  echo "BACKUP=\"\${VOLUME%/}/Library/Application Support/Venosta Visuals/Backups/$NAME-\$(date +%Y%m%d-%H%M%S)-\$\$\""
  echo "for N in $NAME ${RETIRE[*]:-}; do"
  cat <<'SCRIPT'
  if [ -e "$DEST/$N.plugin" ]; then
    /bin/mkdir -p "$BACKUP"
    /usr/bin/ditto "$DEST/$N.plugin" "$BACKUP/$N.plugin"
  fi
done
SCRIPT
  if [[ ${#RETIRE[@]} -gt 0 ]]; then
    echo "# Retired effects of older versions (backed up above)."
    echo "for N in ${RETIRE[*]}; do /bin/rm -rf \"\$DEST/\$N.plugin\"; done"
  fi
} > "$STAGE/scripts/preinstall"
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
  --scripts "$STAGE/scripts" --identifier "$PKGID" \
  --version "$VERSION" --install-location / --ownership recommended "$OUTPUT"

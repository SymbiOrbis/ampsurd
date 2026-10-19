#!/bin/bash
# Builds the macOS installer AMPSURD-<version>-macOS.pkg (standalone app + VST3 + Audio Unit) and
# zips of the single bundles. Run on a Mac after the Release build (the CI does this):
#   installer/macos/build_pkg.sh <version> <artefacts dir> <output dir>
# e.g. installer/macos/build_pkg.sh 0.9.1 build/plugin/Ampsurd_artefacts/Release installer/output
#
# Code signing: NOT active. The bundles get an ad-hoc signature only (Apple Silicon refuses to run
# code without any signature); the .pkg is unsigned and not notarized, so macOS asks the user to
# allow it in System Settings -> Privacy & Security ("Open Anyway"). See docs/CODE_SIGNING.md.
set -Eeuo pipefail
# in GitHub Actions the failing command shows up as an annotation (readable without the log)
trap 'echo "::error title=build_pkg.sh::line $LINENO failed: $BASH_COMMAND"' ERR

VERSION="$1"
ART="$2"
OUT="$3"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT"

APP="$ART/Standalone/AMPSURD.app"
VST3="$ART/VST3/AMPSURD.vst3"
AU="$ART/AU/AMPSURD.component"
for b in "$APP" "$VST3" "$AU"; do
  [ -d "$b" ] || { echo "missing $b"; exit 1; }
  codesign --force --deep --sign - "$b"   # ad-hoc signature (not a Developer ID)
  codesign --verify --deep --strict "$b"
done

# one component package per part; not relocatable, so macOS always installs to the given folder
component() { # <id> <bundle> <install location> <pkg name> [scripts dir]
  local id="$1" bundle="$2" location="$3" name="$4" scripts="${5:-}"
  local stage="$WORK/stage-$name"
  mkdir -p "$stage"
  ditto "$bundle" "$stage/$(basename "$bundle")"
  pkgbuild --analyze --root "$stage" "$WORK/$name.plist"
  # (pkgbuild may not list a .vst3 as a bundle: then there is nothing that could be relocated)
  local i=0
  while /usr/libexec/PlistBuddy -c "Print :$i" "$WORK/$name.plist" >/dev/null 2>&1; do
    /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$WORK/$name.plist" 2>/dev/null \
      || /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$WORK/$name.plist"
    i=$((i + 1))
  done
  local extra=()
  [ -n "$scripts" ] && extra=(--scripts "$scripts")
  pkgbuild --root "$stage" --component-plist "$WORK/$name.plist" --identifier "$id" --version "$VERSION" \
           --install-location "$location" ${extra[@]+"${extra[@]}"} "$WORK/$name.pkg"
}
component com.ampsurd.ampsurd.app  "$APP"  /Applications                       app
component com.ampsurd.ampsurd.vst3 "$VST3" /Library/Audio/Plug-Ins/VST3       vst3
component com.ampsurd.ampsurd.au   "$AU"   /Library/Audio/Plug-Ins/Components au "$HERE/scripts-au"

mkdir -p "$WORK/resources"
cp "$ROOT/LICENSE.md" "$WORK/resources/LICENSE.txt"
sed "s/@VERSION@/$VERSION/g" "$HERE/welcome.html" > "$WORK/resources/welcome.html"
sed "s/@VERSION@/$VERSION/g" "$HERE/distribution.xml" > "$WORK/distribution.xml"

productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" --package-path "$WORK" \
             "$OUT/AMPSURD-$VERSION-macOS.pkg"

ditto -c -k --keepParent "$APP"  "$OUT/AMPSURD-$VERSION-Standalone-macOS.zip"
ditto -c -k --keepParent "$VST3" "$OUT/AMPSURD-$VERSION-VST3-macOS.zip"
ditto -c -k --keepParent "$AU"   "$OUT/AMPSURD-$VERSION-AU-macOS.zip"
ls -l "$OUT"

#!/bin/bash
# Builds the macOS installer AMPSURD-macOS.pkg (standalone app + VST3 + Audio Unit) and AMPSURD-macOS.zip
# (the three bundles). Fixed names without the version: the website links to .../releases/latest/download/<name>. Run on a Mac after the Release build (the CI does this):
#   installer/macos/build_pkg.sh <version> <artefacts dir> <output dir>
# e.g. installer/macos/build_pkg.sh 0.9.2 build/plugin/Ampsurd_artefacts/Release installer/output
#
# Code signing (2026-10-24): when AMPSURD_SIGN_APP is set (the CI does this when the repository has the
# Apple secrets), the bundles are signed with that Developer ID Application identity (hardened runtime,
# secure timestamp; the app with installer/macos/entitlements-app.plist), the .pkg with AMPSURD_SIGN_INSTALLER
# (Developer ID Installer), and - with AMPSURD_NOTARY_KEY / _KEY_ID / _ISSUER - the .pkg is notarized,
# the .pkg and the bundles are stapled and everything is verified (any problem fails the build) before the
# zips are made. Without AMPSURD_SIGN_APP: the old unsigned build (ad-hoc signatures; macOS then asks the user
# to allow the .pkg in System Settings -> Privacy & Security). AMPSURD_KEYCHAIN = the keychain with the
# identities. $OUT/macos-signing.txt says which one was built ("notarized" / "unsigned").
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
SIGN_APP="${AMPSURD_SIGN_APP:-}"
SIGN_PKG="${AMPSURD_SIGN_INSTALLER:-}"
KC="${AMPSURD_KEYCHAIN:-}"
NOTARY_KEY="${AMPSURD_NOTARY_KEY:-}"
kcargs=()
[ -n "$KC" ] && kcargs=(--keychain "$KC")

for b in "$APP" "$VST3" "$AU"; do
  [ -d "$b" ] || { echo "missing $b"; exit 1; }
  if [ -n "$SIGN_APP" ]; then
    # inner code first (frameworks / dylibs / helpers, if a bundle ever has any), then the bundle itself
    while IFS= read -r inner; do
      codesign --force --options runtime --timestamp --sign "$SIGN_APP" ${kcargs[@]+"${kcargs[@]}"} "$inner"
    done < <(find "$b/Contents" \( -name "*.dylib" -o -name "*.framework" -o -name "*.so" \) -prune -print 2>/dev/null)
    ent=()
    [ "$b" = "$APP" ] && ent=(--entitlements "$HERE/entitlements-app.plist")
    codesign --force --options runtime --timestamp --sign "$SIGN_APP" ${ent[@]+"${ent[@]}"} ${kcargs[@]+"${kcargs[@]}"} "$b"
  else
    codesign --force --deep --sign - "$b"   # ad-hoc signature (not a Developer ID)
  fi
  codesign --verify --deep --strict --verbose=2 "$b"
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
if [ -n "$SIGN_APP" ] && [ -n "$NOTARY_KEY" ]; then
  NOTE='Signed and notarized by Apple (Developer ID: Radovan Glovna).'
else
  NOTE='This build is <b>not signed or notarized by Apple</b>. If you have opened this installer, you have already allowed it in System Settings \&rarr; Privacy \&amp; Security.'
fi
sed -e "s/@VERSION@/$VERSION/g" -e "s|@SIGNING_NOTE@|$NOTE|" "$HERE/welcome.html" > "$WORK/resources/welcome.html"
sed "s/@VERSION@/$VERSION/g" "$HERE/distribution.xml" > "$WORK/distribution.xml"

PKG="$OUT/AMPSURD-macOS.pkg"
pkgsign=()
[ -n "$SIGN_PKG" ] && pkgsign=(--sign "$SIGN_PKG" --timestamp ${kcargs[@]+"${kcargs[@]}"})
productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" --package-path "$WORK" \
             ${pkgsign[@]+"${pkgsign[@]}"} "$PKG"
[ -n "$SIGN_PKG" ] && pkgutil --check-signature "$PKG"

# notarize (the .pkg carries all three bundles), staple, verify - before any zip is made
notarize() { # <file to submit>
  local out id status
  out="$WORK/notary-$(basename "$1").json"
  xcrun notarytool submit "$1" --key "$NOTARY_KEY" --key-id "$AMPSURD_NOTARY_KEY_ID" --issuer "$AMPSURD_NOTARY_ISSUER" \
        --wait --timeout 45m --output-format json > "$out" || true
  cat "$out"
  id=$(/usr/bin/plutil -extract id raw -o - "$out" 2>/dev/null || true)
  status=$(/usr/bin/plutil -extract status raw -o - "$out" 2>/dev/null || true)
  if [ "$status" != "Accepted" ]; then
    echo "::error title=notarization::$(basename "$1"): ${status:-no result}"
    [ -n "$id" ] && xcrun notarytool log "$id" --key "$NOTARY_KEY" --key-id "$AMPSURD_NOTARY_KEY_ID" --issuer "$AMPSURD_NOTARY_ISSUER" || true
    exit 1
  fi
}
SIGNING_STATE=unsigned
if [ -n "$SIGN_APP" ] && [ -n "$SIGN_PKG" ] && [ -n "$NOTARY_KEY" ]; then
  notarize "$PKG"
  xcrun stapler staple "$PKG"
  # the bundles' tickets come with the .pkg's notarization; if one is not found, notarize the bundles too
  if ! { xcrun stapler staple "$APP" && xcrun stapler staple "$VST3" && xcrun stapler staple "$AU"; }; then
    ditto -c -k --keepParent "$APP" "$WORK/bundles-app.zip"
    mkdir -p "$WORK/b" && ditto "$VST3" "$WORK/b/$(basename "$VST3")" && ditto "$AU" "$WORK/b/$(basename "$AU")"
    ditto -c -k "$WORK/b" "$WORK/bundles-plugins.zip"
    notarize "$WORK/bundles-app.zip"
    notarize "$WORK/bundles-plugins.zip"
    xcrun stapler staple "$APP"; xcrun stapler staple "$VST3"; xcrun stapler staple "$AU"
  fi
  # verify: any problem fails the build
  for b in "$APP" "$VST3" "$AU"; do
    codesign --verify --deep --strict --verbose=2 "$b"
    xcrun stapler validate "$b"
  done
  spctl -a -vv "$APP" 2>&1 | tee "$WORK/spctl-app.txt"
  grep -q "accepted" "$WORK/spctl-app.txt" || { echo "::error title=spctl::the app is not accepted by Gatekeeper"; exit 1; }
  spctl -a -t install -vv "$PKG" 2>&1 | tee "$WORK/spctl-pkg.txt"
  grep -q "accepted" "$WORK/spctl-pkg.txt" || { echo "::error title=spctl::the .pkg is not accepted by Gatekeeper"; exit 1; }
  xcrun stapler validate "$PKG"
  SIGNING_STATE=notarized
elif [ -n "$SIGN_APP" ]; then
  echo "::warning title=signing::signed but NOT notarized (no notary key) - treat as unsigned for users"
fi
echo "$SIGNING_STATE" > "$OUT/macos-signing.txt"

# one zip with the three (signed, stapled) bundles
mkdir -p "$WORK/zip"
ditto "$APP" "$WORK/zip/$(basename "$APP")"
ditto "$VST3" "$WORK/zip/$(basename "$VST3")"
ditto "$AU" "$WORK/zip/$(basename "$AU")"
ditto -c -k "$WORK/zip" "$OUT/AMPSURD-macOS.zip"
ls -l "$OUT"

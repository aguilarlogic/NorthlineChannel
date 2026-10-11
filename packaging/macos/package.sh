#!/usr/bin/env bash
# Signs, packages, notarizes and staples the macOS build.
#
# Required environment:
#   NL_DEV_ID_APP        "Developer ID Application: <Name> (<TEAMID>)"
#   NL_DEV_ID_INSTALLER  "Developer ID Installer: <Name> (<TEAMID>)"
#   NL_NOTARY_PROFILE    keychain profile created with: xcrun notarytool store-credentials <profile> ...
# Usage: packaging/macos/package.sh <version> <artefacts dir, e.g. build/NorthlineChannel_artefacts/Release>

set -euo pipefail
VERSION="${1:?version}"
ART="${2:?artefacts dir}"
OUT="dist"
WORK="$(mktemp -d)"
ID="com.northlineaudio.channel"
mkdir -p "$OUT"

sign() {
    codesign --force --timestamp --options runtime --sign "$NL_DEV_ID_APP" "$1"
    codesign --verify --deep --strict --verbose=2 "$1"
}

components=()
add_component() { # <name> <bundle path> <install location>
    local name="$1" bundle="$2" dest="$3"
    [ -e "$bundle" ] || { echo "skip $name (not built)"; return; }
    sign "$bundle"
    mkdir -p "$WORK/$name/root"
    cp -R "$bundle" "$WORK/$name/root/"
    pkgbuild --root "$WORK/$name/root" --identifier "$ID.$name" --version "$VERSION" \
             --install-location "$dest" "$WORK/$name.pkg"
    components+=("$name")
}

add_component vst3 "$ART/VST3/Northline Channel.vst3" "/Library/Audio/Plug-Ins/VST3"
add_component au "$ART/AU/Northline Channel.component" "/Library/Audio/Plug-Ins/Components"
add_component clap "$ART/CLAP/Northline Channel.clap" "/Library/Audio/Plug-Ins/CLAP"
add_component app "$ART/Standalone/Northline Channel.app" "/Applications"

# Distribution file with one choice per component.
DIST="$WORK/distribution.xml"
{
    echo '<?xml version="1.0" encoding="utf-8"?>'
    echo '<installer-gui-script minSpecVersion="2">'
    echo "  <title>Northline Channel $VERSION</title>"
    echo '  <license file="EULA.txt"/>'
    echo '  <options customize="always" require-scripts="false" hostArchitectures="arm64,x86_64"/>'
    echo '  <choices-outline>'
    for c in "${components[@]}"; do echo "    <line choice=\"$c\"/>"; done
    echo '  </choices-outline>'
    for c in "${components[@]}"; do
        echo "  <choice id=\"$c\" title=\"$c\"><pkg-ref id=\"$ID.$c\"/></choice>"
        echo "  <pkg-ref id=\"$ID.$c\">$c.pkg</pkg-ref>"
    done
    echo '</installer-gui-script>'
} > "$DIST"

mkdir -p "$WORK/resources"
cp docs/legal/EULA.txt "$WORK/resources/EULA.txt"
PKG="$OUT/NorthlineChannel-$VERSION-mac.pkg"
productbuild --distribution "$DIST" --package-path "$WORK" --resources "$WORK/resources" \
             --sign "$NL_DEV_ID_INSTALLER" "$PKG"

xcrun notarytool submit "$PKG" --keychain-profile "$NL_NOTARY_PROFILE" --wait
xcrun stapler staple "$PKG"
spctl --assess --type install --verbose "$PKG"
shasum -a 256 "$PKG" > "$PKG.sha256"
echo "Done: $PKG"

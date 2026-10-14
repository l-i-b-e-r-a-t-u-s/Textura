#!/bin/sh
# Builds dist/Textura-<version>-mac.pkg: AU and VST3 for every user, and the app.
set -e
cd "$(dirname "$0")/.."
VERSION=${1:-1.0.0}
ART=build/Textura_artefacts/Release
STAGE=$(mktemp -d)
mkdir -p "$STAGE/Library/Audio/Plug-Ins/Components" "$STAGE/Library/Audio/Plug-Ins/VST3" "$STAGE/Applications" dist
cp -R "$ART/AU/Textura.component" "$STAGE/Library/Audio/Plug-Ins/Components/"
cp -R "$ART/VST3/Textura.vst3" "$STAGE/Library/Audio/Plug-Ins/VST3/"
cp -R "$ART/Standalone/Textura.app" "$STAGE/Applications/"
find "$STAGE" -name .DS_Store -delete
xattr -cr "$STAGE"
for b in "$STAGE/Library/Audio/Plug-Ins/Components/Textura.component" "$STAGE/Library/Audio/Plug-Ins/VST3/Textura.vst3" "$STAGE/Applications/Textura.app"; do
    codesign --force --deep -s - "$b"
done
# keep the installer from "finding" another copy of the app and installing over that instead
pkgbuild --analyze --root "$STAGE" "$STAGE.plist" > /dev/null
for i in 0 1 2; do
    /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$STAGE.plist" 2>/dev/null || true
done
rm -f "dist/Textura-$VERSION-mac.pkg"
pkgbuild --root "$STAGE" --component-plist "$STAGE.plist" --identifier org.liberatus.textura.pkg --version "$VERSION" \
         --ownership recommended --install-location / "dist/Textura-$VERSION-mac.pkg"
rm -rf "$STAGE" "$STAGE.plist"
echo "dist/Textura-$VERSION-mac.pkg"

#!/bin/sh
# Copies the built AU and VST3 into the user plug-in folders (remove then copy, so a bundle a
# DAW has loaded is never overwritten in place). Quit the DAW first.
set -e
cd "$(dirname "$0")/.."
ART=build/Textura_artefacts/Release
AU=~/Library/Audio/Plug-Ins/Components
VST3=~/Library/Audio/Plug-Ins/VST3

mkdir -p "$AU"
rm -rf "$AU/Textura.component"
cp -R "$ART/AU/Textura.component" "$AU/"
codesign --force --deep -s - "$AU/Textura.component"

SUDO=""
[ -w "$VST3" ] || SUDO="sudo"
$SUDO mkdir -p "$VST3"
$SUDO rm -rf "$VST3/Textura.vst3"
$SUDO cp -R "$ART/VST3/Textura.vst3" "$VST3/"
$SUDO codesign --force --deep -s - "$VST3/Textura.vst3"

echo "Installed AU + VST3. Validate the AU with: auval -v aumu Txtr Lbts"

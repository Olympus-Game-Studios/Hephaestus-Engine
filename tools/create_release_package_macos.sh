#!/bin/bash
# Hephaestus Engine - Create macOS Release Package
# Assembles "Hephaestus Engine.app" from a macos-release build: the launcher opens it, the editor
# sits beside it in Contents/MacOS, and Qt, plugins, the game player and tools are bundled inside.
# Signs with a Developer ID when --sign is given (ad-hoc otherwise) and notarizes with --notary-profile.
#
#   tools/create_release_package_macos.sh [--sign "Developer ID Application: Name (TEAMID)"]
#                                         [--notary-profile <notarytool keychain profile>]
#                                         [--output <dir>]
#
# Store a notary profile once with:
#   xcrun notarytool store-credentials <profile> --apple-id <id> --team-id <TEAMID>
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/macos-release/Release"
OUTPUT="$ROOT/dist/macos"
IDENTITY="-"
NOTARY_PROFILE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --sign) IDENTITY="$2"; shift 2 ;;
        --notary-profile) NOTARY_PROFILE="$2"; shift 2 ;;
        --output) OUTPUT="$2"; shift 2 ;;
        -h|--help) sed -n 2,12p "$0"; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done
if [[ -n "$NOTARY_PROFILE" && "$IDENTITY" == "-" ]]; then
    echo "Notarization needs a Developer ID: pass --sign as well." >&2
    exit 1
fi
for app in HephaestusLauncher HephaestusEngine HephaestusGame; do
    [[ -d "$BUILD/$app.app" ]] || { echo "Missing $BUILD/$app.app; run: cmake --build --preset macos-release" >&2; exit 1; }
done

APP="$OUTPUT/Hephaestus Engine.app"
CONTENTS="$APP/Contents"
RESOURCES="$CONTENTS/Resources"
echo "Assembling $APP"
rm -rf "$APP"
mkdir -p "$OUTPUT"
cp -R "$BUILD/HephaestusLauncher.app" "$APP"
cp "$BUILD/HephaestusEngine.app/Contents/MacOS/HephaestusEngine" "$CONTENTS/MacOS/"
/usr/libexec/PlistBuddy -c "Set :CFBundleName Hephaestus Engine" \
    -c "Set :CFBundleIdentifier $(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' \
        "$BUILD/HephaestusEngine.app/Contents/Info.plist")" "$CONTENTS/Info.plist"
for item in assets editor scripts .engine shaders Plugins tools HephaestusGame.app; do
    if [[ -e "$BUILD/$item" ]]; then cp -R "$BUILD/$item" "$RESOURCES/"; fi
done

echo "Bundling Lua, Vulkan and MoltenVK for the editor"
VULKAN_LIB="$(sed -n 's/^Vulkan_LIBRARY:FILEPATH=//p' "$BUILD/../CMakeCache.txt")"
MOLTENVK_LIB="$(sed -n 's/^HEPHAESTUS_MOLTENVK_DYLIB:FILEPATH=//p' "$BUILD/../CMakeCache.txt")"
MOLTENVK_ICD="$(sed -n 's/^HEPHAESTUS_MOLTENVK_ICD:FILEPATH=//p' "$BUILD/../CMakeCache.txt")"
cmake -DAPP="$APP" -DEXE="$CONTENTS/MacOS/HephaestusEngine" -DVULKAN="$VULKAN_LIB" \
      -DMOLTENVK="$MOLTENVK_LIB" -DMOLTENVK_ICD="$MOLTENVK_ICD" \
      -P "$ROOT/cmake/BundleMacRuntime.cmake"

echo "Bundling Qt"
MACDEPLOYQT="$(command -v macdeployqt || true)"
[[ -n "$MACDEPLOYQT" ]] || { echo "macdeployqt not found (it ships with Qt 6)" >&2; exit 1; }
deploy_args=(-executable="$CONTENTS/MacOS/HephaestusEngine")
while IFS= read -r -d '' plugin; do
    deploy_args+=(-executable="$plugin")
done < <(find "$RESOURCES/Plugins" -name '*.dylib' -print0)
"$MACDEPLOYQT" "$APP" "${deploy_args[@]}" -always-overwrite
for exe in HephaestusEngine HephaestusLauncher; do
    if ! otool -l "$CONTENTS/MacOS/$exe" | grep -q "@executable_path/../Frameworks"; then
        install_name_tool -add_rpath "@executable_path/../Frameworks" "$CONTENTS/MacOS/$exe"
    fi
done
# Plugins load into the editor; drop the build machine's Qt from their search paths.
while IFS= read -r -d '' plugin; do
    { otool -l "$plugin" | awk '/LC_RPATH/{getline; getline; print $2}' | grep '^/' || true; } | while read -r path; do
        install_name_tool -delete_rpath "$path" "$plugin"
    done
done < <(find "$RESOURCES/Plugins" -name '*.dylib' -print0)

echo "Signing with: $IDENTITY"
SIGN=(codesign --force --sign "$IDENTITY")
if [[ "$IDENTITY" != "-" ]]; then SIGN+=(--options runtime --timestamp); fi
is_macho() { file -b "$1" | grep -q 'Mach-O'; }
sign_loose_code() { # every Mach-O under $1 outside nested bundles, innermost first
    find "$1" -type f -not -path '*.app/*' -not -path '*.framework/*' -print0 |
        while IFS= read -r -d '' file; do
            if is_macho "$file"; then "${SIGN[@]}" "$file"; fi
        done
}
GAME="$RESOURCES/HephaestusGame.app"
sign_loose_code "$GAME/Contents/Frameworks"
"${SIGN[@]}" "$GAME"
find "$CONTENTS/Frameworks" -maxdepth 1 -name '*.framework' -print0 |
    while IFS= read -r -d '' framework; do "${SIGN[@]}" "$framework"; done
sign_loose_code "$CONTENTS/Frameworks"
sign_loose_code "$CONTENTS/PlugIns"
sign_loose_code "$RESOURCES"
"${SIGN[@]}" --entitlements "$ROOT/tools/macos/HephaestusEditor.entitlements" "$CONTENTS/MacOS/HephaestusEngine"
"${SIGN[@]}" --entitlements "$ROOT/tools/macos/HephaestusEditor.entitlements" "$APP"
codesign --verify --deep --strict "$APP"

DMG="$OUTPUT/Hephaestus-Engine.dmg"
echo "Creating $DMG"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
rm -f "$DMG"
hdiutil create -volname "Hephaestus Engine" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null
if [[ "$IDENTITY" != "-" ]]; then codesign --force --sign "$IDENTITY" --timestamp "$DMG"; fi

if [[ -n "$NOTARY_PROFILE" ]]; then
    echo "Notarizing (this can take a few minutes)"
    xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$DMG"
    xcrun stapler staple "$APP"
fi
echo "Done: $APP and $DMG"

#!/bin/bash
# Hephaestus Engine - Create macOS Release Package
# Builds Release from this checkout and assembles "Hephaestus Engine.app": the launcher opens it,
# the editor sits beside it in Contents/MacOS, and Qt, plugins, the game player and tools are
# bundled inside, without tests or development-only content. Signs with a Developer ID when
# --sign is given (ad-hoc otherwise), notarizes with --notary-profile, and also writes a DMG
# unless --no-dmg is given (the Steam content folder is the output folder holding only the app).
#
#   tools/create_release_package_macos.sh [--sign "Developer ID Application: Name (TEAMID)"]
#                                         [--notary-profile <notarytool keychain profile>]
#                                         [--output <dir>] [--build-dir <dir>] [--qt6-dir <dir>]
#                                         [--no-dmg]
#
# Store a notary profile once with:
#   xcrun notarytool store-credentials <profile> --apple-id <id> --team-id <TEAMID>
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build/steam-macos"
QT6_DIR="$HOME/Qt/6.10.2/macos/lib/cmake/Qt6"
OUTPUT="$ROOT/dist/macos"
IDENTITY="-"
NOTARY_PROFILE=""
MAKE_DMG=1
# Plugins that only exist for development, not for users.
EXCLUDED_PLUGINS=(HephaestusFPS)

while [[ $# -gt 0 ]]; do
    case "$1" in
        --sign) IDENTITY="$2"; shift 2 ;;
        --notary-profile) NOTARY_PROFILE="$2"; shift 2 ;;
        --output) OUTPUT="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --qt6-dir) QT6_DIR="$2"; shift 2 ;;
        --no-dmg) MAKE_DMG=0; shift ;;
        -h|--help) sed -n 2,15p "$0"; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done
if [[ -n "$NOTARY_PROFILE" && "$IDENTITY" == "-" ]]; then
    echo "Notarization needs a Developer ID: pass --sign as well." >&2
    exit 1
fi
MACDEPLOYQT="$QT6_DIR/../../../bin/macdeployqt"
if [[ ! -f "$QT6_DIR/Qt6Config.cmake" || ! -x "$MACDEPLOYQT" ]]; then
    echo "Qt not found at $QT6_DIR (expected Qt6Config.cmake and ../../../bin/macdeployqt). Pass --qt6-dir." >&2
    exit 1
fi

echo "Building Release in $BUILD_DIR with Qt from $QT6_DIR"
if [[ ! -f "$ROOT/extern/JoltPhysics/Build/CMakeLists.txt" || ! -f "$ROOT/extern/glfw/CMakeLists.txt" ||
      ! -f "$ROOT/extern/ufbx/ufbx.h" ]]; then
    git -C "$ROOT" submodule update --init --recursive
fi
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DQt6_DIR="$QT6_DIR" \
      -DHEPHAESTUS_BUILD_EDITOR=ON -DHEPHAESTUS_BUILD_GAME=ON -DHEPHAESTUS_USE_QT=ON \
      -DHEPHAESTUS_BUILD_COMPAT_TESTS=OFF -DHEPHAESTUS_BUILD_QT_TESTS=OFF \
      -DHEPHAESTUS_RUNTIME_AUTOMATION=OFF
cmake --build "$BUILD_DIR"
BUILD="$BUILD_DIR/Release"
for app in HephaestusLauncher HephaestusEngine HephaestusGame; do
    [[ -d "$BUILD/$app.app" ]] || { echo "The build did not produce $BUILD/$app.app" >&2; exit 1; }
done

# Publish only after signing and notarization succeed, so a failed run keeps the previous app and DMG.
mkdir -p "$OUTPUT"
WORK="$(mktemp -d "$OUTPUT/.release.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
APP="$WORK/Hephaestus Engine.app"
CONTENTS="$APP/Contents"
RESOURCES="$CONTENTS/Resources"
echo "Assembling $APP"
cp -R "$BUILD/HephaestusLauncher.app" "$APP"
cp "$BUILD/HephaestusEngine.app/Contents/MacOS/HephaestusEngine" "$CONTENTS/MacOS/"
/usr/libexec/PlistBuddy -c "Set :CFBundleName Hephaestus Engine" \
    -c "Set :CFBundleIdentifier $(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' \
        "$BUILD/HephaestusEngine.app/Contents/Info.plist")" "$CONTENTS/Info.plist"
for item in assets editor scripts .engine shaders Plugins tools HephaestusGame.app LICENSE.md THIRD_PARTY_NOTICES.md; do
    if [[ -e "$BUILD/$item" ]]; then cp -R "$BUILD/$item" "$RESOURCES/"; fi
done
rm -rf "$RESOURCES/assets/tests"
for plugin in "${EXCLUDED_PLUGINS[@]}"; do rm -rf "$RESOURCES/Plugins/$plugin"; done

echo "Bundling Lua, Vulkan and MoltenVK for the editor"
VULKAN_LIB="$(sed -n 's/^Vulkan_LIBRARY:FILEPATH=//p' "$BUILD/../CMakeCache.txt")"
MOLTENVK_LIB="$(sed -n 's/^HEPHAESTUS_MOLTENVK_DYLIB:FILEPATH=//p' "$BUILD/../CMakeCache.txt")"
MOLTENVK_ICD="$(sed -n 's/^HEPHAESTUS_MOLTENVK_ICD:FILEPATH=//p' "$BUILD/../CMakeCache.txt")"
cmake -DAPP="$APP" -DEXE="$CONTENTS/MacOS/HephaestusEngine" -DVULKAN="$VULKAN_LIB" \
      -DMOLTENVK="$MOLTENVK_LIB" -DMOLTENVK_ICD="$MOLTENVK_ICD" \
      -P "$ROOT/cmake/BundleMacRuntime.cmake"

echo "Bundling Qt"
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
is_macho() { [[ "$(file -b "$1")" == *Mach-O* ]]; } # not a grep -q pipe: SIGPIPE + pipefail
sign_loose_code() { # every Mach-O under $1 outside nested bundles, innermost first
    find "$1" -type f -not -path "$1/*.app/*" -not -path "$1/*.framework/*" -print0 |
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

DMG="$WORK/Hephaestus-Engine.dmg"
if [[ "$MAKE_DMG" == 1 ]]; then
    echo "Creating $DMG"
    STAGE="$WORK/dmg"
    mkdir "$STAGE"
    cp -R "$APP" "$STAGE/"
    ln -s /Applications "$STAGE/Applications"
    hdiutil create -volname "Hephaestus Engine" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null
    if [[ "$IDENTITY" != "-" ]]; then codesign --force --sign "$IDENTITY" --timestamp "$DMG"; fi
fi

if [[ -n "$NOTARY_PROFILE" ]]; then
    echo "Notarizing (this can take a few minutes)"
    if [[ "$MAKE_DMG" == 1 ]]; then
        xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait
        xcrun stapler staple "$DMG"
    else
        ditto -c -k --keepParent "$APP" "$WORK/notarize.zip"
        xcrun notarytool submit "$WORK/notarize.zip" --keychain-profile "$NOTARY_PROFILE" --wait
    fi
    xcrun stapler staple "$APP"
fi

PUBLISHED_APP="$OUTPUT/Hephaestus Engine.app"
PUBLISHED_DMG="$OUTPUT/Hephaestus-Engine.dmg"
rm -rf "$PUBLISHED_APP"
mv "$APP" "$PUBLISHED_APP"
if [[ "$MAKE_DMG" == 1 ]]; then
    mv -f "$DMG" "$PUBLISHED_DMG"
    echo "Done: $PUBLISHED_APP and $PUBLISHED_DMG"
else
    rm -f "$PUBLISHED_DMG"
    echo "Done: $PUBLISHED_APP"
fi

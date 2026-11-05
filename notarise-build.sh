#!/bin/bash
# Builds Release, then signs, notarizes and staples "Hephaestus Engine.app" and its DMG in
# dist/macos (see tools/create_release_package_macos.sh). Uses the first "Developer ID Application" certificate in the
# keychain and the "hephaestus-notary" notarytool profile unless these are set:
#   HEPHAESTUS_SIGN_IDENTITY  e.g. "Developer ID Application: Your Name (TEAMID)"
#   HEPHAESTUS_NOTARY_PROFILE e.g. my-notary-profile
set -euo pipefail
cd "$(dirname "$0")"

IDENTITY="${HEPHAESTUS_SIGN_IDENTITY:-$(security find-identity -v -p codesigning |
    sed -n 's/.*"\(Developer ID Application: .*\)"/\1/p' | head -n 1)}"
PROFILE="${HEPHAESTUS_NOTARY_PROFILE:-hephaestus-notary}"
if [[ -z "$IDENTITY" ]]; then
    echo "No Developer ID Application certificate found in the keychain." >&2
    exit 1
fi

tools/create_release_package_macos.sh --sign "$IDENTITY" --notary-profile "$PROFILE"

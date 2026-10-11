#!/usr/bin/env bash
# Replace the ad-hoc signature with a Developer ID signature and notarise the
# DMG so Gatekeeper accepts it on other Macs. Requires an Apple Developer
# account; unsigned (ad-hoc) builds still run on the machine that built them
# and on others after right-click > Open.
#
#   packaging/macos/sign-and-notarize.sh <path/to/App.app> <path/to/output.dmg>
#
# Environment:
#   DEVELOPER_ID   "Developer ID Application: Name (TEAMID)"
#   NOTARY_PROFILE keychain profile created with `xcrun notarytool store-credentials`
set -euo pipefail

app="$1"
dmg="$2"
: "${DEVELOPER_ID:?set DEVELOPER_ID}"
: "${NOTARY_PROFILE:?set NOTARY_PROFILE}"

codesign --force --deep --options runtime --timestamp --sign "$DEVELOPER_ID" "$app"
codesign --verify --deep --strict --verbose=2 "$app"

rm -f "$dmg"
hdiutil create -volname "$(basename "$app" .app)" -srcfolder "$app" -ov -format UDZO "$dmg"
codesign --force --timestamp --sign "$DEVELOPER_ID" "$dmg"

xcrun notarytool submit "$dmg" --keychain-profile "$NOTARY_PROFILE" --wait
xcrun stapler staple "$dmg"
echo "Signed and notarised: $dmg"

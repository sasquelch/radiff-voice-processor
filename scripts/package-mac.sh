#!/usr/bin/env bash
# Collects macOS release artifacts into <dist>/ and zips them. Signing/notarization is
# optional and handled by scripts/sign-mac.sh when Developer ID credentials exist.
set -euo pipefail
BUILD=${1:?build dir}; DIST=${2:?dist dir}
VERSION=$(grep -E 'project\(RaDiffVoiceProcessor' -A1 "$(dirname "$0")/../CMakeLists.txt" | grep -oE 'VERSION [0-9.]+' | awk '{print $2}')
STAGE="$DIST/RaDiff-Voice-Processor-$VERSION-macOS"
rm -rf "$STAGE"; mkdir -p "$STAGE"
cp "$BUILD/bin/RaDiffVoiceCLI" "$STAGE/"
# Standalone app: iPlug2 places it in <build>/out (see src/standalone/CMakeLists.txt).
[[ -d "$BUILD/out/RaDiffVoice.app" ]]                   && cp -R "$BUILD/out/RaDiffVoice.app" "$STAGE/"
[[ -d "$BUILD/bin/RaDiff Voice Processor.vst3" ]]       && cp -R "$BUILD/bin/RaDiff Voice Processor.vst3" "$STAGE/"
[[ -d "$BUILD/bin/RaDiff Voice Processor.component" ]]  && cp -R "$BUILD/bin/RaDiff Voice Processor.component" "$STAGE/"
cp -R "$(dirname "$0")/../presets" "$STAGE/Presets"
cp "$(dirname "$0")/../docs/INSTALL-macOS.md" "$STAGE/README.md" 2>/dev/null || true
(cd "$DIST" && ditto -c -k --sequesterRsrc --keepParent "$(basename "$STAGE")" "$(basename "$STAGE").zip")
echo "Packaged $DIST/$(basename "$STAGE").zip"

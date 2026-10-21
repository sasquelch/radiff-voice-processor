#!/usr/bin/env bash
# Configure, build and test on macOS.
#   scripts/build-mac.sh                # Debug, native arch
#   scripts/build-mac.sh release        # Release, native arch
#   scripts/build-mac.sh universal      # Release, arm64 + x86_64 (distribution build)
#   scripts/build-mac.sh asan           # Debug with Address/UB sanitizers
set -euo pipefail
cd "$(dirname "$0")/.."

case "${1:-debug}" in
  debug)     PRESET=mac-debug ;;
  release)   PRESET=mac-release ;;
  universal) PRESET=mac-universal-release ;;
  asan)      PRESET=mac-asan ;;
  *) echo "unknown mode '$1' (debug|release|universal|asan)"; exit 2 ;;
esac

cmake --preset "$PRESET"
cmake --build --preset "$PRESET" --parallel
ctest --preset "$PRESET" --output-on-failure
echo "Artifacts in build/$PRESET/bin"

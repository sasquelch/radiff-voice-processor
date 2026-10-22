#!/usr/bin/env bash
# Verifies (and, with --install, installs) the macOS development dependencies.
# Required: Xcode or Command Line Tools, CMake >= 3.25, Ninja, git. Nothing else.
set -euo pipefail

INSTALL=0
[[ "${1:-}" == "--install" ]] && INSTALL=1

ok()   { printf '  \033[32m✓\033[0m %s\n' "$*"; }
miss() { printf '  \033[31m✗\033[0m %s\n' "$*"; }

echo "RaDiff Voice Processor - macOS environment check"
echo "  macOS $(sw_vers -productVersion) ($(uname -m))"

if xcode-select -p >/dev/null 2>&1; then
  ok "Xcode / Command Line Tools: $(xcode-select -p)"
else
  miss "Xcode Command Line Tools missing"; [[ $INSTALL == 1 ]] && xcode-select --install
fi

if command -v clang++ >/dev/null; then ok "$(clang++ --version | head -1)"; else miss "clang++ missing"; fi

if command -v brew >/dev/null; then
  ok "Homebrew $(brew --version | head -1 | awk '{print $2}')"
else
  miss "Homebrew missing (https://brew.sh) - needed only to install cmake/ninja"
fi

need_brew=()
if command -v cmake >/dev/null; then ok "$(cmake --version | head -1)"; else miss "cmake missing"; need_brew+=(cmake); fi
if command -v ninja >/dev/null; then ok "ninja $(ninja --version)"; else miss "ninja missing"; need_brew+=(ninja); fi
if command -v git >/dev/null; then ok "$(git --version)"; else miss "git missing"; fi

if (( ${#need_brew[@]} )) && [[ $INSTALL == 1 ]]; then
  brew install "${need_brew[@]}"
elif (( ${#need_brew[@]} )); then
  echo "Run: brew install ${need_brew[*]}   (or re-run with --install)"
fi

echo "Submodules:"
cd "$(dirname "$0")/.."
git submodule update --init third_party/nlohmann_json third_party/doctest third_party/iPlug2 third_party/minimp3
git submodule update --init third_party/vst3sdk
git -C third_party/vst3sdk submodule update --init base pluginterfaces public.sdk cmake
git submodule status | sed 's/^/  /'
echo "Done. Next: scripts/build-mac.sh"

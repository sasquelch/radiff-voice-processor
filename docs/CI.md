# Continuous integration

`.github/workflows/ci.yml` runs two independent jobs on every push to `main`, every pull
request and on manual dispatch. Both check out the same commit with submodules.

| Job | Runner | Steps |
|---|---|---|
| macOS | `macos-15` (Apple Silicon) | configure `mac-universal-release` → build → verify `lipo`/`minos 11.0` → ctest (arm64) → test binaries under Rosetta (x86_64) → VST3 validator* → auval* → package → upload |
| Windows | `windows-2022` | MSVC x64 dev shell → configure `windows-x64-release` → build → ctest → VST3 validator* → portable ZIP → upload |

\* Plug-in validation steps activate automatically once `src/plugins/*` contain sources (Phase 6).

Pinned: `actions/checkout@v4`, `actions/upload-artifact@v4`, `lukka/get-cmake` with CMake
3.31.6 / Ninja 1.12.1, `ilammy/msvc-dev-cmd@v1`.

## Free-tier limits (GitHub Actions, as of October 2026)

- **Public repository:** unlimited minutes on GitHub-hosted runners, including macOS.
- **Private repository, Free plan:** 2,000 minutes/month, 500 MB artifact storage.
  Windows minutes count ×2 and macOS minutes count ×10 against that quota. A ~6-minute
  macOS job plus a ~4-minute Windows job therefore costs ~68 quota-minutes per run,
  roughly 29 runs per month.
- Artifacts expire after 90 days by default.

## Building without hosted CI

Hosted CI is optional. Everything the workflow does can be run locally:

- **Mac:** `scripts/build-mac.sh universal` then `scripts/package-mac.sh`.
- **Windows PC:** install the free Visual Studio 2022 Build Tools, CMake, Ninja and Git,
  then `scripts\build-windows.ps1` and `scripts\package-windows.ps1`.
- **Self-hosted runner:** any Windows 10/11 PC (or a Windows VM on the Mac) can be
  registered as a free self-hosted GitHub Actions runner; change `runs-on: windows-2022`
  to `runs-on: [self-hosted, windows, x64]`. No paid build service is needed.

Cross-compiling Windows binaries from macOS is deliberately **not** used: MSVC and the
Windows SDK are required for native Windows builds, and the result would not be the
artifact we ship.

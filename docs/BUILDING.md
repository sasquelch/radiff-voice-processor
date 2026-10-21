# Building

## Toolchains

| | macOS | Windows |
|---|---|---|
| Compiler | Apple clang (Xcode 15+ or Command Line Tools) | MSVC v143 (Visual Studio 2022 Build Tools, free) |
| SDK | macOS SDK from Xcode | Windows 10/11 SDK (installed with the C++ workload) |
| Build | CMake ≥ 3.25, Ninja | CMake ≥ 3.25, Ninja (or the VS generator) |
| Language | C++20 (`-std=c++20`) | C++20 (`/std:c++20 /permissive-`) |
| Deployment | **macOS 11.0** minimum, Universal arm64 + x86_64 | **Windows 10 x64** minimum (`_WIN32_WINNT=0x0A00`) |

Verified development machine: macOS 27.0.1, Apple M1 Max, Xcode 27.1 (Apple clang 21),
CMake 4.4.3, Ninja 1.13.2. Nothing Windows-specific is required on the Mac; nothing
Apple-specific is required on Windows.

### Why macOS 11.0?

Big Sur is the first release that runs on Apple Silicon, so one Universal binary covers
every Mac that can run the product. Both pinned dependencies support it (iPlug2 ≥ 10.13,
VST3 SDK 3.8 ≥ 10.13). The standard library on this target lacks floating-point
`std::from_chars`; `radiff::platform::parseNumber` exists for that reason.

## CMake presets

| Preset | Purpose |
|---|---|
| `mac-debug` | day-to-day development, native architecture |
| `mac-asan` | Debug + AddressSanitizer/UBSan |
| `mac-release` | optimised, native architecture |
| `mac-universal-release` | **distribution build**, arm64 + x86_64 |
| `mac-xcode` | Xcode project (attach the debugger to a DAW) |
| `windows-x64-debug` / `windows-x64-release` | Ninja + MSVC from a VS developer shell |
| `windows-vs2022` | Visual Studio solution |

```
cmake --preset <name>
cmake --build --preset <name> --parallel
ctest --preset <name> --output-on-failure
```

## Options

| Option | Default | Notes |
|---|---|---|
| `RADIFF_BUILD_CLI` | ON | |
| `RADIFF_BUILD_TESTS` | ON | doctest unit tests + CTest CLI integration tests |
| `RADIFF_BUILD_STANDALONE` | OFF | Phase 5 |
| `RADIFF_BUILD_VST3` | OFF | Phase 6 |
| `RADIFF_BUILD_AU` | OFF | Phase 6, macOS only |
| `RADIFF_UNIVERSAL` | OFF | macOS only; set by `mac-universal-release` |
| `RADIFF_WARNINGS_AS_ERRORS` | ON | `-Werror` / `/WX` for first-party code only |
| `RADIFF_ENABLE_ASAN` | OFF | clang/gcc only |

## Dependencies

All dependencies are git submodules pinned to exact commits (see `third_party/README.md`).
A fresh clone needs `git clone --recursive`, or `scripts/bootstrap-mac.sh`, which also
limits the VST3 SDK checkout to the modules we use (no `vstgui4`, no docs).

## Verifying a Universal binary

```bash
lipo -info build/mac-universal-release/bin/RaDiffVoiceCLI   # x86_64 arm64
otool -l build/mac-universal-release/bin/RaDiffVoiceCLI | grep -A3 LC_BUILD_VERSION   # minos 11.0
arch -x86_64 build/mac-universal-release/bin/test_core    # needs Rosetta 2 on Apple Silicon
```

Rosetta 2 is not installed by default on Apple Silicon; `softwareupdate --install-rosetta`
adds it. CI's macOS runner has it and runs the test binaries under both architectures.

## Code signing (macOS) and the cost of distribution

- Local development builds are unsigned and run fine on the build machine.
- Release builds can be signed and notarized with `scripts/sign-mac.sh` (Phase 9) when
  `DEVELOPER_ID_APPLICATION`, `APPLE_ID`, `APPLE_TEAM_ID` and an app-specific password are
  provided. This requires a paid Apple Developer Program membership (US$99/year).
- Without notarization, Gatekeeper will block the app and plug-ins on other Macs until the
  user right-clicks → Open, or removes the quarantine attribute. Public macOS distribution
  is therefore **not** zero-cost; private/internal use is.

## Windows unsigned builds

Windows SmartScreen shows "Windows protected your PC" for unsigned executables downloaded
from the internet; users click "More info → Run anyway". Local builds and files copied
over a LAN are not affected. Authenticode certificates are optional and not required by
the build.

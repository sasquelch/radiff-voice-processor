# RaDiff Voice Processor

Cross-platform broadcast voice processor for macOS and Windows: standalone application,
headless command-line processor, VST3 plug-in (both platforms) and AUv2 plug-in (macOS),
all driven by one original, framework-independent C++20 DSP engine and one portable
`.radvoice.json` preset format.

| Artifact | macOS | Windows | Status |
|---|---|---|---|
| `RaDiffVoiceCLI` / `RaDiffVoiceCLI.exe` | Universal (arm64 + x86_64) | x64 | **Builds and passes tests on macOS**; Windows pending first CI run |
| `RaDiff Voice Processor.app` / `RaDiffVoiceProcessor.exe` | planned (Phase 5) | planned (Phase 5) | not started |
| `RaDiff Voice Processor.vst3` | planned (Phase 6) | planned (Phase 6) | not started |
| `RaDiff Voice Processor.component` (AUv2) | planned (Phase 6) | n/a | not started |

See [docs/STATUS.md](docs/STATUS.md) for the honest, per-platform verification matrix.

## Quick start (macOS)

```bash
scripts/bootstrap-mac.sh --install   # checks Xcode CLT, cmake, ninja; pulls pinned submodules
scripts/build-mac.sh                 # Debug build + all tests
scripts/build-mac.sh universal       # Release Universal binary + tests
build/mac-universal-release/bin/RaDiffVoiceCLI --help
```

## Quick start (Windows 10/11 x64)

Install the free Visual Studio 2022 Build Tools ("Desktop development with C++"), CMake and
Ninja, then:

```powershell
git clone --recursive <repo-url>
.\scripts\build-windows.ps1
build\windows-x64-release\bin\RaDiffVoiceCLI.exe --help
```

## Command line

Identical arguments, exit codes and processing on both platforms:

```bash
# macOS
./RaDiffVoiceCLI --input "/Users/example/input.wav" --output "/Users/example/output.wav" --preset "/Users/example/Voice.radvoice.json"
```
```bat
:: Windows
RaDiffVoiceCLI.exe --input "D:\Audio\input.wav" --output "D:\Audio\output.wav" --preset "D:\Audio\Voice.radvoice.json"
```

Exit codes: `0` success, `2` usage, `3` input, `4` preset, `5` processing, `6` output, `70` internal.
Output files are written to a hidden temporary file beside the destination, flushed to disk,
re-opened and validated, then atomically renamed into place. A failed run never leaves a
partial or missing deliverable.

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) – library layering and the platform-abstraction rules
- [docs/BUILDING.md](docs/BUILDING.md) – toolchains, presets, options, deployment targets
- [docs/CI.md](docs/CI.md) – GitHub Actions jobs, free-tier limits, building without hosted CI
- [docs/PRESET-FORMAT.md](docs/PRESET-FORMAT.md) – the `.radvoice.json` schema
- [docs/STATUS.md](docs/STATUS.md) – verification matrix (compiles / tests / validators / DAW / listening / release)
- [docs/LICENSES.md](docs/LICENSES.md) – third-party licence review for closed-source distribution
- [third_party/README.md](third_party/README.md) – pinned dependency versions

## Licence

Proprietary. Copyright (c) 2026. All rights reserved. Third-party components are listed in
`docs/LICENSES.md`.

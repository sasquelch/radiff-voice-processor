# RaDiff Voice Processor

Cross-platform broadcast voice processor for macOS and Windows: standalone application,
headless command-line processor, VST3 plug-in (both platforms) and AUv2 plug-in (macOS),
all driven by one original, framework-independent C++20 DSP engine and one portable
`.radvoice.json` preset format.

| Artifact | macOS | Windows | Status |
|---|---|---|---|
| `RaDiffVoiceCLI` / `RaDiffVoiceCLI.exe` | Universal (arm64 + x86_64) | x64 | **Real DSP chain, builds and passes 31 tests on macOS**; Windows authored, not yet compiled (no CI minutes / no Windows PC) |
| `RaDiff Voice Processor.app` / `RaDiffVoiceProcessor.exe` | planned (Phase 5) | planned (Phase 5) | not started |
| `RaDiff Voice Processor.vst3` | planned (Phase 6) | planned (Phase 6) | not started |
| `RaDiff Voice Processor.component` (AUv2) | planned (Phase 6) | n/a | not started |

See [docs/DEVELOPMENT_STATUS.md](docs/DEVELOPMENT_STATUS.md) for the honest, per-platform
verification matrix and [MASTER_DEVELOPMENT_SPEC.md](MASTER_DEVELOPMENT_SPEC.md) for the
authoritative specification.

**Processing chain (v1):** input trim → DC blocker → 12/24 dB/oct high-pass → 5-band EQ →
speech-preserving downward expander (soft knee, hysteresis, hold, lookahead, hard attenuation
cap) → leveling compressor → density compressor → output trim. De-esser, multiband expander,
presence, saturation and limiter follow in Phase 6.

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

Exit codes: `0` verified success, `2` usage, `3` input, `4` preset, `5` processing, `6` output I/O,
`7` validation, `8` cancelled, `70` internal. `--overwrite` is required to replace an existing
output; the input is never overwritten. `--set expander.thresholdDbfs=-44` overrides any
parameter after the preset (`--list-params` prints them all); `--json-result` prints a
machine-readable summary; `--analyze-only` measures a file (speech level, noise floor, peak,
resolved expander threshold) without writing anything. Factory presets place the expander
threshold relative to the speech level the analysis pass measures in each file.
Output files are written to a hidden temporary file beside the destination, flushed to disk,
re-opened and validated, then atomically renamed into place. A failed run never leaves a
partial or missing deliverable.

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) – library layering and the platform-abstraction rules
- [docs/BUILDING.md](docs/BUILDING.md) – toolchains, presets, options, deployment targets
- [docs/CI.md](docs/CI.md) – GitHub Actions jobs, free-tier limits, building without hosted CI
- [docs/PRESET-FORMAT.md](docs/PRESET-FORMAT.md) – the `.radvoice.json` schema
- [docs/DSP_DESIGN.md](docs/DSP_DESIGN.md) – algorithms, detector/ballistics semantics, latency
- [docs/DEVELOPMENT_STATUS.md](docs/DEVELOPMENT_STATUS.md) – phase progress and verification matrix
- [docs/TEST_RESULTS.md](docs/TEST_RESULTS.md) – executed test and real-audio results with commands
- [docs/RELEASE_CHECKLIST.md](docs/RELEASE_CHECKLIST.md) – release gates
- [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) – third-party licence review for closed-source distribution
- [CLAUDE.md](CLAUDE.md) – working conventions, build commands, active milestone
- [third_party/README.md](third_party/README.md) – pinned dependency versions

## Licence

Proprietary. Copyright (c) 2026. All rights reserved. Third-party components are listed in
`THIRD_PARTY_LICENSES.md`.

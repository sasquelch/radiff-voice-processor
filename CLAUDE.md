# CLAUDE.md — RaDiff Voice Processor

**Authoritative specification:** [MASTER_DEVELOPMENT_SPEC.md](MASTER_DEVELOPMENT_SPEC.md) (v1.0, 2026-10-21).
Read it before changing DSP, presets, CLI behaviour or the shipping matrix. It supersedes the
earlier cross-platform amendment. Status lives in [docs/DEVELOPMENT_STATUS.md](docs/DEVELOPMENT_STATUS.md);
executed test evidence in [docs/TEST_RESULTS.md](docs/TEST_RESULTS.md).

## Currently active milestone

**Phase 2 — real DSP engine: done for input stage, 5-band EQ, capped downward expander,
compressor 1 + 2.** Next: Phase 3 remainder (CLI options `--sample-rate`, `--channels`,
`--analyze-only`, `--log`, dither on bit-depth reduction), then Phase 4 standalone GUI (iPlug2).
Phase 6 stages (de-esser, multiband expander, presence, saturation, limiter) are not implemented
and have no placeholder parameters.

## Build and test (macOS, the development machine)

```bash
scripts/bootstrap-mac.sh --install      # once: checks Xcode CLT/cmake/ninja, pulls pinned submodules
scripts/build-mac.sh                    # Debug + all tests          (preset mac-debug)
scripts/build-mac.sh asan               # Address/UB sanitizers      (preset mac-asan)
scripts/build-mac.sh universal          # Release arm64+x86_64       (preset mac-universal-release)
ctest --preset mac-debug --output-on-failure
build/mac-debug/bin/test_core           # doctest binaries accept -tc="*expander*" etc.
```

Windows: `scripts\build-windows.ps1` on a Windows 10/11 x64 PC with the free VS 2022 Build Tools.
**GitHub Actions is manual-only** (owner's quota is exhausted, 2026-10-21); do not re-enable push
triggers. Windows code paths are authored and unverified until run on a Windows machine.

## Machine-specific notes (verified 2026-10-21)

- Apple M1 Max, macOS 27.0.1, Xcode 27.1 (Apple clang 21), CMake 4.4.3, Ninja 1.13.2, gh (user sasquelch).
- Rosetta 2 is NOT installed: the x86_64 slice builds but cannot be executed here.
- DAWs present: REAPER, Adobe Audition 2024/2025. Logic Pro is NOT installed (AU host test = unverified).
- Minimum macOS deployment target is 11.0; float `std::from_chars` is unavailable there, use
  `radiff::platform::parseNumber` / `core::parseParamValue`.
- Real voice material for listening tests: `~/Desktop/RaDiff-Listening/` (A-raw-* vs B-<preset>-*).

## Rules that keep the codebase honest

- `src/core` links only the C++ standard library. No OS headers, no `#ifdef _WIN32`/`__APPLE__`
  (architecture guards in `dsp/Denormals.h` are the single exception). No allocation in `process()`.
- OS code lives only in `src/platform/{posix,macos,windows}` behind the headers in
  `src/platform/include/radiff/platform/`.
- Parameters are declared once in `core/Parameters.h` (`RADIFF_PARAM_LIST`). **Append only**;
  the enum index is the stable automation/state ID. Bump `core::kAlgorithmVersion` when a change
  alters the audible transfer function.
- Every DSP change needs a deterministic test in `tests/core/` that measures the behaviour
  (level, response, timing), not just "it runs". Run `ctest --preset mac-debug` before committing.
- Never claim a platform/host is verified unless it was actually executed; keep
  `docs/DEVELOPMENT_STATUS.md` and `docs/TEST_RESULTS.md` current.
- Presets never contain paths or machine data. CLI output is transactional (temp file → verify →
  atomic replace) and exit code 0 means verified success only.

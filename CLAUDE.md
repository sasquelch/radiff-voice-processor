# CLAUDE.md — RaDiff Voice Processor

**Authoritative specification:** [MASTER_DEVELOPMENT_SPEC.md](MASTER_DEVELOPMENT_SPEC.md) (v1.0, 2026-10-21).
Read it before changing DSP, presets, CLI behaviour or the shipping matrix. It supersedes the
earlier cross-platform amendment. Status lives in [docs/DEVELOPMENT_STATUS.md](docs/DEVELOPMENT_STATUS.md);
executed test evidence in [docs/TEST_RESULTS.md](docs/TEST_RESULTS.md).

## Currently active milestone

**Phase 2 — real DSP engine: done for input stage, 5-band EQ, capped downward expander,
compressor 1 + 2.** Listening round 1 (owner: clicks + breaths audible) led to the
speech-relative expander threshold (analysis pass, `Engine::setSpeechLevelDb`), transient
rejection and re-tuned presets; round 2: the owner approved the **medium** tier (= Balanced
preset defaults) but still heard mouse clicks → per-sample click ducker (`clickDuckDb`, on in
all factory presets, equal to the cap); round 3 on `~/Desktop/RaDiff-Listening/D-medium-clickduck-*` pending.
**Phase 6 (partial, 2026-10-21):** de-esser (`modules/Deesser`), presence/enhancer with LF/HF
Detail (`modules/Presence`), compressor `autoMakeup`, and `Engine::setSpeechTracking()` (online
speech level for live input) exist with tests; all OFF in factory presets. The owner asked for
every dbx 286s control to have a counterpart: the mapping table is in docs/DSP_DESIGN.md.
Multiband expander, saturation and limiter are not implemented and have no placeholder parameters.
**Phase 4 (first version, 2026-10-21):** iPlug2 standalone `RaDiffVoice-app` in `src/standalone`
(+ toolkit-free `radiff::ui` in `src/ui`), controls generated from the parameter table, preset
library, file panel, preview playback through the live chain (`ui::FilePlayer`), live speech tracking. Build: `cmake --preset mac-debug -DRADIFF_BUILD_STANDALONE=ON`
(default ON on Apple/Windows for fresh caches), app at `build/<preset>/out/RaDiffVoice.app`;
`RADIFF_SCREENSHOT=<png>` renders and exits (see docs/STANDALONE.md).
Next: owner hands-on with the app, then Phase 3 remainder (`--sample-rate`, `--channels`, `--log`,
dither), then VST3/AU wrappers reusing `radiff::ui`.

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
- Real voice material for listening tests: `~/Desktop/RaDiff-Listening/` (A-raw-* vs
  B2-{gentle,medium,strong}-*; Phase 2 renders in `old-phase2/`). Measured there: breaths and
  mouse clicks sit ≈ 21 dB under the speech level, so absolute expander thresholds are useless;
  keep presets speech-relative.

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

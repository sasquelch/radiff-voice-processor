# Development status

Updated 2026-10-21 after listening round 2 and the first Phase 6 stages. Compilation alone is never counted as "working".
Spec phases: see MASTER_DEVELOPMENT_SPEC.md §13.

## Phase progress

| Phase | State |
|---|---|
| 1 Foundation | **Done.** CMake/C++20 layered tree, parameter registry, WAV I/O, transactional output, CLI skeleton, tests, pinned deps, CI workflow authored. |
| 2 Real DSP engine | **Done for the required stages:** input trim/DC/HPF, 5-band EQ, capped downward expander (now with speech-relative threshold, analysis pass and transient rejection after listening round 1), compressor 1 and 2, meters, latency, aligned bypass. 59 deterministic test cases. Listening round 2: owner chose the medium expander tier (= Balanced preset defaults). |
| 3 CLI, WAV I/O, presets | **Mostly done:** full nested `.radvoice.json` schema with validation/warnings, 10 factory presets, `--set`, `--format/--bit-depth`, `--overwrite`, `--json-result`, `--verbose`, `--analyze-only` (speech level, noise floor, peak, resolved threshold), exit codes 0/2/3/4/5/6/7/8/70, latency compensation, silence validation. **Open:** `--sample-rate`, `--channels`, `--log`, dither when reducing bit depth, Windows compilation. |
| 4 Standalone GUI | **First working version (macOS, 2026-10-21):** iPlug2 APP target `RaDiffVoice-app`, every registry parameter has a control (generated from the table), EQ curve, meters (in/out, EXP/DS/C1/C2 reduction), preset menu with factory + user presets, New/Load/Save/Save As/Delete, dirty state, file panel (analyze/process one WAV on a worker thread, same `app::processFile` as the CLI, verified bit-identical to a CLI render), live input with online speech tracking, **file preview playback through the live chain** (Load/Play/Pause/Stop/Loop/seek, resampled to the device rate, meters live). **Open:** matched-loudness A/B, batch, waveform, drag-drop, transfer curves, Windows build (resources authored only). See docs/STANDALONE.md. |
| 5 VST3 / AUv2 | Not started (VST3 SDK 3.8.1 pinned; `src/plugins/*` placeholders). |
| 6 Advanced processing | **Partly done (2026-10-21):** frequency-selective de-esser and presence/enhancer (LF Detail, HF Detail with program-dependent back-off) implemented with tests; compressor auto makeup; online speech-level tracker for realtime hosts. All OFF in factory presets until heard. **Open:** multiband expander, saturation, limiter (no placeholder parameters; presets referencing them load with warnings). |
| 7 PlayoutONE integration | Not started. |
| 8 Release | Not started. |

## Verification matrix

Legend: ✅ verified · 🟡 built but not executed · ⬜ not yet · — n/a

| Artifact / platform | Compiles | Automated tests | Plug-in validator | Real DAW | Listening approved | Distributable |
|---|---|---|---|---|---|---|
| CLI · macOS arm64 | ✅ Debug, ASan/UBSan, Release | ✅ 34/34 CTest, `test_core` 73 cases / 4841 assertions, real audio | — | — | ✅ round 2: medium tier approved by the owner on 4 real files (expander only; full chain re-listen after Phase 6) | ⬜ |
| CLI · macOS x86_64 (Universal slice) | ✅ `lipo` both slices, `minos 11.0` | 🟡 Rosetta 2 not installed on the dev Mac | — | — | ⬜ | ⬜ |
| CLI · Windows x64 | ⬜ **authored, not executed** (no Windows machine; GitHub Actions quota exhausted → workflow is manual-only) | ⬜ | — | — | ⬜ | ⬜ |
| Standalone · macOS arm64 | ✅ Debug + Release (NanoVG/Metal) | ✅ `test_ui` preset library; app launched, rendered (self-screenshot), file job ran | — | — | ⬜ not yet heard live | ⬜ unsigned, not notarised |
| Standalone · Windows x64 | ⬜ authored, not executed | ⬜ | — | — | ⬜ | ⬜ |
| VST3 · macOS / Windows | ⬜ | ⬜ | ⬜ | ⬜ REAPER + Audition 2024/2025 available on the Mac; no Windows host | ⬜ | ⬜ |
| AUv2 · macOS | ⬜ | ⬜ | ⬜ auval | ⬜ Logic Pro not installed | ⬜ | ⬜ |
| Presets cross-platform | ✅ schema + canonical bytes | ✅ round trip, spec example, defaults, clamps, BOM, 10 factory presets | — | — | — | ⬜ parity across OSes pending a Windows build |

## Known blockers and risks

- **Windows verification** comes from the GitHub Actions Windows job (repository made public on
  2026-10-21, so runners are free). Until its first green run every Windows code path (UTF-16
  paths, `ReplaceFileW`, `CommandLineToArgvW`, `CreateProcessW`, the app's resources) is unverified.
- **Listening approval** is a release gate (spec §2). Round 1 found clicks and breaths; the
  cause (absolute −48 dBFS threshold far below real breath levels, gate open during hold) is
  fixed. Round 2: the owner chose the medium tier, which is the Balanced-preset default, but
  still heard mouse clicks: those are sub-millisecond and sit in the hold/release, so a per-sample
  click ducker (`expander.clickDuckDb`, on in all factory presets) was added; renders
  `D-medium-clickduck-*` await round 3. The full chain must be heard again once Phase 6 is complete.
- **GUI** exists for macOS (Phase 4 first version); file dialogs, the preset popup and the
  Save As/Delete flows were exercised in code and by the preset-library test, not yet by a
  person clicking through them. The app is unsigned (Gatekeeper: right-click → Open).
- **Realtime hosts** now have `Engine::setSpeechTracking()` (gated level with 20 s forgetting,
  tested with tones); it has not yet been heard on live speech, and the first second of a
  session uses the absolute threshold. The offline path (CLI, file mode) is complete.
- **No limiter yet**: presets must keep headroom by themselves; CHR preset makeup was removed
  after a full-scale source reached 0 dBFS.
- x86_64 slice untested locally (install Rosetta 2 with `softwareupdate --install-rosetta` to run it).

## Next steps (in order)

1. ~~Listening round 2~~ done: medium tier chosen (2026-10-21).
2. Owner hands-on with the standalone app (settings, save/load, live input); fix what they find.
3. Listening check of de-esser / enhancer on real voice; decide whether any factory preset enables them.
4. Finish Phase 3 CLI options (`--sample-rate`, `--channels`, `--log`) and dither.
4. Phase 5: VST3 + AU wrappers, validator/auval, REAPER/Audition hands-on.
5. Remaining Phase 6 stages (multiband expander, saturation, limiter) with tests before any factory preset enables them.

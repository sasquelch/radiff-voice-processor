# Development status

Updated 2026-10-21 after Phase 2. Compilation alone is never counted as "working".
Spec phases: see MASTER_DEVELOPMENT_SPEC.md §13.

## Phase progress

| Phase | State |
|---|---|
| 1 Foundation | **Done.** CMake/C++20 layered tree, parameter registry, WAV I/O, transactional output, CLI skeleton, tests, pinned deps, CI workflow authored. |
| 2 Real DSP engine | **Done for the required stages:** input trim/DC/HPF, 5-band EQ, capped downward expander, compressor 1 and 2, meters, latency, aligned bypass. 50 deterministic test cases. Audition by a human still pending. |
| 3 CLI, WAV I/O, presets | **Mostly done:** full nested `.radvoice.json` schema with validation/warnings, 10 factory presets, `--set`, `--format/--bit-depth`, `--overwrite`, `--json-result`, `--verbose`, exit codes 0/2/3/4/5/6/7/8/70, latency compensation, silence validation. **Open:** `--sample-rate`, `--channels`, `--analyze-only`, `--log`, dither when reducing bit depth, Windows compilation. |
| 4 Standalone GUI | Not started (iPlug2 pinned; `src/standalone`, `src/ui` placeholders). |
| 5 VST3 / AUv2 | Not started (VST3 SDK 3.8.1 pinned; `src/plugins/*` placeholders). |
| 6 Advanced processing | Not started: de-esser, multiband expander, presence, saturation, limiter. No placeholder parameters exist; presets referencing them load with warnings. |
| 7 PlayoutONE integration | Not started. |
| 8 Release | Not started. |

## Verification matrix

Legend: ✅ verified · 🟡 built but not executed · ⬜ not yet · — n/a

| Artifact / platform | Compiles | Automated tests | Plug-in validator | Real DAW | Listening approved | Distributable |
|---|---|---|---|---|---|---|
| CLI · macOS arm64 | ✅ Debug, ASan/UBSan, Release | ✅ 31/31 CTest, 4637 assertions, real audio | — | — | ⬜ | ⬜ |
| CLI · macOS x86_64 (Universal slice) | ✅ `lipo` both slices, `minos 11.0` | 🟡 Rosetta 2 not installed on the dev Mac | — | — | ⬜ | ⬜ |
| CLI · Windows x64 | ⬜ **authored, not executed** (no Windows machine; GitHub Actions quota exhausted → workflow is manual-only) | ⬜ | — | — | ⬜ | ⬜ |
| Standalone · macOS / Windows | ⬜ | ⬜ | — | — | ⬜ | ⬜ |
| VST3 · macOS / Windows | ⬜ | ⬜ | ⬜ | ⬜ REAPER + Audition 2024/2025 available on the Mac; no Windows host | ⬜ | ⬜ |
| AUv2 · macOS | ⬜ | ⬜ | ⬜ auval | ⬜ Logic Pro not installed | ⬜ | ⬜ |
| Presets cross-platform | ✅ schema + canonical bytes | ✅ round trip, spec example, defaults, clamps, BOM, 10 factory presets | — | — | — | ⬜ parity across OSes pending a Windows build |

## Known blockers and risks

- **Windows verification** needs either a Windows 10/11 PC (`scripts\build-windows.ps1`) or
  GitHub Actions minutes (public repo = free; or a self-hosted runner). Until then every
  Windows code path (UTF-16 paths, `ReplaceFileW`, `CommandLineToArgvW`, `CreateProcessW`) is unverified.
- **Listening approval** is a release gate (spec §2) and has not happened. Material is staged in
  `~/Desktop/RaDiff-Listening/`.
- **No limiter yet**: presets must keep headroom by themselves; CHR preset makeup was removed
  after a full-scale source reached 0 dBFS.
- x86_64 slice untested locally (install Rosetta 2 with `softwareupdate --install-rosetta` to run it).

## Next steps (in order)

1. Human listening pass on the staged A/B material; record findings in `docs/TEST_RESULTS.md`.
2. Finish Phase 3 CLI options and dither; add `--analyze-only` metrics.
3. Phase 4: iPlug2 standalone (macOS first), wiring the shared engine/preset code.
4. Phase 5: VST3 + AU wrappers, validator/auval, REAPER/Audition hands-on.
5. Phase 6 stages with tests before any factory preset enables them.

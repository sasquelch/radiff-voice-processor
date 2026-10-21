# Verification status

Updated 2026-10-21 (end of Phase 1). Compilation alone is never counted as "working".

Legend: ✅ verified · 🟡 built but not executed · ⬜ not yet · ❌ failing · — not applicable

| Artifact / platform | Compiles | Automated tests pass | Plug-in validator | Tested in a real DAW | Listening tests approved | Ready for distribution |
|---|---|---|---|---|---|---|
| CLI · macOS arm64 | ✅ Debug, Release, ASAN/UBSan | ✅ 27/27 (unit + CLI integration, real audio files) | — | — | ⬜ (placeholder DSP) | ⬜ |
| CLI · macOS x86_64 (Universal slice) | ✅ `lipo` shows both slices, `minos 11.0` | 🟡 Rosetta 2 not installed on dev Mac; CI runs tests under `arch -x86_64` | — | — | ⬜ | ⬜ |
| CLI · Windows x64 | ⬜ awaiting first CI run (needs GitHub remote) or a Windows PC | ⬜ | — | — | ⬜ | ⬜ |
| Standalone · macOS | ⬜ Phase 5 | ⬜ | — | — | ⬜ | ⬜ |
| Standalone · Windows | ⬜ Phase 5 | ⬜ | — | — | ⬜ | ⬜ |
| VST3 · macOS | ⬜ Phase 6 | ⬜ | ⬜ validator | ⬜ REAPER, Audition 2024/2025 available on dev Mac | ⬜ | ⬜ |
| VST3 · Windows | ⬜ Phase 6 | ⬜ | ⬜ validator (CI) | ⬜ no Windows DAW host available yet | ⬜ | ⬜ |
| AUv2 · macOS | ⬜ Phase 6 | ⬜ | ⬜ auval | ⬜ Logic Pro not installed on dev Mac | ⬜ | ⬜ |
| Presets cross-platform | ✅ format + canonical serialisation | ✅ round trip, defaults, clamping, BOM, unknown keys | — | — | — | ⬜ parity test across OSes pending Windows CI |
| PlayoutONE WebVT wrapper | ⬜ Phase 8 | ⬜ | — | — | — | ⬜ |

## What "automated tests pass" currently covers

- Engine: pass-through, dB gain, bypass, block-size independence, finite output, silence.
- Parameters: table integrity, clamping, stepping, equality.
- WAV I/O: 16/24/32-bit PCM + float32 round trip within ½ LSB, `WAVE_FORMAT_EXTENSIBLE`
  with unknown/odd-sized chunks, error messages, clipping, Unicode file names.
- Presets: canonical JSON, defaults, warnings, clamping, BOM tolerance, rejection cases.
- Transactional output: success replaces atomically, failed write/validation preserves the
  original and leaves no temp files, parent directories created, directory targets refused.
- File processing: mono/stereo, odd block sizes, progress, format override, every exit code.
- CLI (black-box, via CTest): `--version`, `--help`, exit codes 2/3/4/6, preset load,
  `--set` overrides, `--format`, Unicode paths, block-size independence, preserved output
  on failure, `--no-overwrite`.
- Real audio: two voice recordings from the development Mac (mono 16-bit, stereo 24-bit)
  processed by the Release Universal CLI; outputs read back by Apple CoreAudio (`afinfo`,
  `afconvert`).

## Known gaps

- The DSP chain is a Phase 1 placeholder (input gain, output gain, bypass). The full chain
  requires the master development specification.
- Windows has not been compiled yet. All Windows code paths (UTF-16 paths, `ReplaceFileW`,
  `CommandLineToArgvW`, `CreateProcessW`) are written but unverified until CI runs.
- Rosetta 2 is not installed on the development Mac, so the x86_64 slice is untested locally.

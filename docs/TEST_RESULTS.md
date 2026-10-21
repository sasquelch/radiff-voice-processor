# Test results (executed)

Only results that were actually executed are recorded here, with the environment and command.
Environment for all entries unless stated: Apple M1 Max, macOS 27.0.1, Xcode 27.1
(Apple clang 21.0.0), CMake 4.4.3, Ninja 1.13.2, repository commit as of the entry date.

## 2026-10-21 — Phase 2 (real DSP engine)

### Automated suites

| Preset | Command | Result |
|---|---|---|
| `mac-debug` (arm64, Debug) | `ctest --preset mac-debug` | 31/31 CTest tests passed; `test_core` 50 cases / 4637 assertions |
| `mac-asan` (arm64, ASan + UBSan) | `ctest --preset mac-asan` | 31/31 passed, no sanitizer reports |
| `mac-universal-release` (arm64 + x86_64, Release) | `ctest --preset mac-universal-release` | 31/31 passed (arm64 slice executed; x86_64 slice built, **not executed**: Rosetta 2 not installed) |
| Windows x64 | — | **not executed** (no Windows machine; GitHub Actions quota exhausted) |

### What the DSP tests measured (tests/core)

- Biquads: LP/HP −3.01 dB at fc, HP −12.3 dB one octave below, bell +6.0/−12.0 dB at centre,
  shelves within 0.05 dB of target, processed sine agrees with analytic response within 0.02 dB,
  impulse responses finite and decayed at every extreme (20 Hz Q 12 … 20 kHz, 44.1–96 kHz).
- Coefficient ramp: switching a flat filter to a +15 dB Q 8 bell mid-stream produces a maximum
  sample step of < 0.06 vs. a larger step with an instantaneous switch.
- Input stage: 12 dB/oct HPF at 80 Hz −3.0/−12.3/−24.1 dB at 80/40/20 Hz; 24 dB/oct at 100 Hz
  −3.0/−24.1 dB at 100/50 Hz; DC offset of +0.3 removed to < 1e-3; gain exact to 0.002 dB.
- EQ: flat EQ bit-identical; cascaded bands add in dB (3 × 2 dB = 6.00 dB); toggles click-free.
- Expander: 2:1 below −30 dBFS moves −40 → −50 dBFS; cap 10 dB turns −60 → −70 (not −90) and
  −75 → −85; soft knee gives −4.5 dB at threshold (R 4, W 12); hold 200 ms measured open at
  100 ms and closed at 450 ms; hysteresis 6 dB keeps −33 dBFS open after −20 and closed after
  −60; release τ = 100 ms reaches −6.3 dB of the −10 dB target at 100 ms; attack 5 ms fully open
  within 25 ms; lookahead 5 ms = 240 samples latency and raises first-millisecond onset level by
  > 6 dB; stereo link vs independent; wet 50 % gives −3.63 dB; sidechain HPF 300 Hz closes on
  50 Hz rumble; peak detector calibration.
- Compressor: 4:1 above −20 dBFS moves −8 → −17 dBFS; 20:1 → −19.4; knee 6 dB gives −0.5625 dB
  at threshold; makeup +6 dB exact; attack τ 10 ms at 63 % and release τ 100 ms at 37 %;
  auto-release recovers > 1 dB slower after 2 s than after a 20 ms burst and is identical when
  off; stereo link; sidechain HPF; wet 50 %; compressor 2 reads its own parameters.
- Engine: all-off chain bit-transparent; full bypass bit-exact including with 480 samples of
  lookahead latency; balanced defaults compress a −10 dBFS tone to −18.4 dBFS and attenuate a
  −60 dBFS floor by exactly 10 dB (meter reads 10.0 dB); block sizes 1/64/1000/4096 differ by
  < 1e-6; 44.1/48/96 kHz agree within 0.01 dB; parameter changes mid-stream are click-free;
  two instances are independent.
- File processor: lookahead latency compensated (impulses at frames 5000 and 19990 land on the
  same frames; frame count unchanged); overwrite opt-in; silent input OK; validation codes.
- CLI (black box): exit codes 2/3/4/6, `--set` with module paths, factory balanced preset
  reduces a −6 dBFS tone to −15.0 dBFS peak after 0.5 s, Unicode paths, JSON result.

### Real audio (CLI, factory presets)

Files: `~/Desktop/RaDiff-Listening/A-raw-*.wav` (TOM0010669: 25 s stereo 16-bit 44.1 kHz voice
track peaking at 0.0 dBFS; 1012am: 46 s mono 16-bit; vikings: 26 s stereo 24-bit).
Outputs read back by Apple CoreAudio (`afinfo`) with identical durations.

| Input | Preset | Output peak | Latency compensated | Note |
|---|---|---|---|---|
| TOM0010669 (0.0 dBFS) | Broadcast Voice – Balanced | −0.9 dBFS | 0 | |
| TOM0010669 | Noisy Room – Conservative | −1.0 dBFS | 221 frames (5 ms @ 44.1 k) | |
| TOM0010669 | CHR Radio – Punchy (first version) | **0.0 dBFS** | 0 | +3 dB makeup on a full-scale source reached full scale → makeup removed from the preset |
| TOM0010669 | CHR Radio – Punchy (revised) | −3.0 dBFS | 0 | |
| 1012am (−8.6 dBFS) | Balanced / Noisy Room / CHR | −10.4 / −10.2 / −11.5 dBFS | 0 / 221 / 0 | |
| vikings (−4.2 dBFS) | Balanced / Noisy Room / CHR | −10.0 / −8.2 / −10.3 dBFS | 0 / 221 / 0 | |

Speed: Release Universal CLI processes the 46 s mono file in 0.16 s (283× real time); Debug
build ≈ 70× real time.

### Listening tests

**Not performed yet.** The A/B material above is prepared for a human listening session
(loudness-match before comparing). No factory preset is approved; descriptions say so.

### Not verified (and therefore not claimed)

Windows compilation and tests; x86_64 execution; any plug-in format; any DAW host; dither on
bit-depth reduction (not implemented yet); long-duration/stress runs.

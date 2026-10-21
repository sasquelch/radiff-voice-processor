# Test results (executed)

Only results that were actually executed are recorded here, with the environment and command.
Environment for all entries unless stated: Apple M1 Max, macOS 27.0.1, Xcode 27.1
(Apple clang 21.0.0), CMake 4.4.3, Ninja 1.13.2, repository commit as of the entry date.

## 2026-10-21 — First Windows build (GitHub Actions, after the repository went public)

Three CI rounds, all on `windows-2022` with MSVC 14.44, CMake 3.31.6, Ninja 1.12.1, preset
`windows-x64-release`; the macOS job passed in every round.

| Round | Result | Cause / fix |
|---|---|---|
| 1 (run 39694900270) | build failed in `test_parameters.cpp` | `std::nanf` without `<cmath>`; replaced by `std::numeric_limits<float>::quiet_NaN()`; CI build now uses `-k 0` so one run shows every error |
| 2 (run 39695279698) | **every library, `RaDiffVoiceCLI.exe`, `RaDiffVoice.exe` and the other test binaries linked**; `test_core` failed to compile | MSVC instantiates `operator<<(ostream&, string_view)` when doctest stringifies values and needs `<ostream>` in the TU; added to every test file |
| 3 (run 39695841863) | ✅ **100 % tests passed, 0 failed out of 34** on Windows; portable ZIP artifact uploaded | — |

What this proves: the shared engine, WAV I/O, preset format (byte-identical canonical files),
transactional output with `ReplaceFileW`, Unicode command-line/paths, the analysis pass, the
preset library and the file player all behave identically on Windows as far as the tests
measure. What it does not prove: anyone has run `RaDiffVoice.exe` or listened on Windows.

## 2026-10-21 — Preview playback in the standalone app

Owner request: load a file and hear it live through the current settings while watching the
levels. `ui::FilePlayer` + a PREVIEW strip (Load, Play/Pause, Stop, Loop, seek bar, time,
source indicator); while playing, the file replaces the device input and the engine runs
exactly as for live audio (meters, bypass A/B, latency, speech tracker).

| What | Result |
|---|---|
| `test_ui` (file player) | ✅ same-rate playback bit-exact; 48 → 44.1 kHz 1 kHz and 6 kHz tones within 0.2 dB RMS; position/seek/pause/loop/ended; mono fills both channels |
| `ctest --preset mac-debug` / `mac-release` | ✅ 34/34 |
| `RADIFF_AUTO_PLAY` hook, 48 kHz file into the 44.1 kHz device stream (resampler path) | ✅ `playing=1 position=13.00s in=-34.6 dBFS … max-in=-7.6 max-out=-10.9 dBFS gr exp=11.2 dB`; screenshot shows IN/OUT meters lit, EXP bar, "SOURCE: FILE PREVIEW", 0:13.0 / 0:39.7, "Threshold in use … tracked live" |

**Not verified:** clicking the transport by hand; matched-loudness A/B (bypass compares at
natural level); waveform display; very long files load synchronously on the UI thread.

## 2026-10-21 — Click ducking (owner: "I'm still hearing clicks of a mouse")

Cause found by measuring the raw take (`bursts.py`, 2 ms and 10 ms one-pole detectors at the
resolved −32.6 dBFS threshold): the mouse clicks are 0.0–1.2 ms long with sample peaks −14 … −37
dBFS. On the 2 ms detector a −23 dBFS click reads about −43 dBFS, far under the threshold, so
the gate's detectors never see them; the ones heard sit 15–150 ms after words, inside hold/release.
Fix: per-sample click ducker in the expander (`clickDuckDb`), see docs/DSP_DESIGN.md.

### Automated

3 new test cases in `test_expander.cpp`: a 0.5 ms click 50 ms after a word is reduced by
20.0 dB (±0.6) while the rest of the render is bit-identical; a plosive onset (8 ms burst, 5 ms
gap, vowel), a 15 ms consonant release and a click during speech are untouched (max diff 0 /
< 1e-6); a click in a closed pause stays at the cap; latency 384 samples with lookahead 0.
Suites: mac-debug 34/34 (`test_core` 76 cases), mac-asan 34/34, mac-release 34/34 (see the
end of this section for the run after preset regeneration).

### Real voice (TOM0010679, Balanced preset = medium tier, Release CLI)

Sample peak within ±3 ms of each click, change versus raw:

| Click | context | raw peak | medium, duck off | medium, duck 20 |
|---|---|---|---|---|
| 27.87 s | 150 ms gap inside a sentence | −14.5 dBFS | −6.1 dB | **−18.5 dB** |
| 36.40 s | pause (gate closed) | −36.5 | −19.8 | −19.8 (cap, unchanged) |
| 36.86 s | 50 ms after a word | −27.8 | −7.8 | **−20.7 dB** |
| 30.02 s | 1 ms transient 26 ms after a word | −21.7 | −0.7 | −15.1 |
| 31.92 s | 77 ms after a word | −19.6 | −7.9 | −11.7 |

Candidates 15–47 ms after a word (7.54 s, 13.64 s, 24.66 s, 24.85 s) are not ducked because
the 10 ms level is still above the threshold there; they are masked by the word tail. Over the
whole 39.7 s take the ducker cut 7 regions by more than 3 dB, all 2.0–4.6 ms long with a quiet
10 ms before them; no speech frame changed by more than compressor-envelope ripple (0.3 dB
max sample difference inside speech frames, caused by compressor 1 re-settling after a dip).
Renders: `~/Desktop/RaDiff-Listening/D-medium-clickduck-<file>.wav` for all four files
(compare with `B2-medium-*`, which is the same preset without ducking). **Not yet heard by the owner.**

## 2026-10-21 — Phase 4: standalone app (macOS)

| What | Result |
|---|---|
| `cmake --preset mac-debug -DRADIFF_BUILD_STANDALONE=ON` + build | ✅ compiles (iPlug2 APP target, NanoVG/Metal, C++20); `radiff::ui` under the strict warning set |
| `ctest --preset mac-debug` | ✅ 34/34 (new `test_ui`: preset library scan/load/dirty/save-as/delete/new against the factory folder and a temp user folder) |
| App launch | ✅ `RaDiffVoice.app` starts, opens the default audio devices via RtAudio 6.0.1, loads "Broadcast Voice - Balanced", renders at 60 fps; self-screenshot `RADIFF_SCREENSHOT` reviewed: all 8 module panels, every parameter control, EQ curve, 6 meters, file bar, no overlaps at 1440×880 (Retina) |
| File panel path | ✅ `RADIFF_AUTO_PROCESS_IN/OUT` hook ran `A-raw-TOM0010679.wav` through the GUI job runner (`app::processFile` on a worker thread): exit 0, report shows speech −20.6 / threshold −32.6; output compared with the CLI render of the same preset: max abs diff 0 (`radiff_testtool compare-wav`) |
| Live tracker in the app | ✅ `Engine::setSpeechTracking(true, 20 s)` wired in `OnReset`; the Expander panel reports "absolute until ~1 s of speech has been heard" and then the tracked threshold (engine behaviour measured in the section below) |

**Not verified:** a person clicking through file dialogs, the preset popup and Save As/Delete
(code paths exist and the library behind them is tested); live audio heard through the app; the
Universal (x86_64) slice of the app; Windows resources/build; code signing/notarisation.

## 2026-10-21 — Phase 6 (part 1): de-esser, enhancer, auto makeup, online speech tracker

Added after the owner asked for every dbx 286s control to have a counterpart. All new stages
default OFF, so the audible chain of the approved medium tier is unchanged (algorithm version
stays 1; factory presets regenerated only to carry the new keys).

### Automated suites

| Preset | Result |
|---|---|
| mac-debug | 33/33 CTest; `test_core` 73 cases / 4841 assertions |
| mac-asan (Address + UB sanitizers) | 33/33, no reports |
| mac-release | 33/33 |

New deterministic tests: `test_deesser.cpp` (band reduced by exactly the cap at the centre
frequency, 1 kHz untouched < 0.1 dB, static curve knee/cap values, 0.5 ms attack reaches the
cap within 5 ms, 20 ms release decays below 0.1 dB in 100 ms, listen = band only, stereo link),
`test_presence.cpp` (LF/HF shelf gains match the designed response, dynamic HF back-off < 0.5 dB
on a lone 10 kHz tone and full boost under a dominant low tone, half sensitivity = 4 dB,
makeup, bit-exact off, click-free enable), compressor auto makeup (−20 → −20, −40 → −35 at
threshold −30 2:1), engine chain/meter, online tracking (−36 dBFS threshold within 3 s, follows
a 10 dB drop within six time constants, parameter changes do not reset it, disable restores
the host value), preset format (old six-module `signalOrder` loads, `listen` never persisted,
`deesser.bandwidthOctaves` path parses).

### Real voice (TOM0010679, Balanced preset, Release CLI; 10 ms Hann frames)

Sibilant frames = speech frames with > 50 % of their energy in 4–10 kHz (101 frames);
vowel-like = < 10 % (1440 frames).

| Setting | 4–10 kHz, sibilant frames | 4–10 kHz, vowel frames | 300 Hz–3 kHz, sibilant | wideband, vowel |
|---|---|---|---|---|
| De-esser 6 kHz, 1 oct, threshold −30 dBFS, cap 6 dB | **−3.2 dB** mean (min −4.5) | −1.5 dB | −0.1 dB | −0.02 dB |

| Setting | 60–200 Hz, speech | 4–12 kHz, vowel frames | 4–12 kHz, sibilant frames | 300 Hz–3 kHz |
|---|---|---|---|---|
| Enhancer LF Detail 4, HF Detail 6, hfDynamic 0.5 | +0.4 dB | **+3.8 dB** | +2.7 dB (backed off) | +0.1 dB |

Reading: the de-esser takes about half its cap off the esses at this threshold (the band peaks
sit in the soft knee), leaves the vowel body untouched and spills 1.5 dB into the high band of
neighbouring vowel frames through its 60 ms release. The enhancer's dynamic back-off gives
sibilant frames 1 dB less boost than vowels; it is tone shaping, not loudness. Renders for
listening: `~/Desktop/RaDiff-Listening/C-deesser-6k-6dB-*`, `C-enhancer-lf4-hf6-*` and
`C-deesser-LISTEN-band-only-*` (the detection band alone). **Not yet heard by the owner**;
no factory preset enables either stage.

### Online speech tracker on real speech (simulated live path)

A scratch program fed TOM0010679 through `core::Engine` in 480-frame blocks with
`setSpeechTracking(true, 20 s)` and the Balanced preset, exactly as a live host will.
Offline two-pass reference: speech −20.6 dBFS, threshold −32.6 dBFS.

| Time into the take | tracked speech level | expander threshold |
|---|---|---|
| 0–7 s (first second of gated speech not yet collected) | none | −48.0 (absolute fallback) |
| 8 s | −17.7 | −29.7 |
| 12 s | −19.2 | −31.2 |
| 16 s | −20.0 | −32.0 |
| 24–30 s | −20.0 … −20.2 | −32.0 … −32.2 |
| end (39.7 s) | −20.9 | −32.9 |

Within 1 dB of the offline measurement after 12 s of speech and within 0.3 dB from 16 s on.
Consequence for live use: the first seconds of a session run on the absolute threshold, so a
host should seed the engine with the last known level (`setSpeechLevelDb`) when it has one;
noted for the standalone.

### Not verified

Windows build; tracker behaviour across operators with very different levels in one session.

## 2026-10-21 — Listening round 1: "mouse clicks and heavy breathing in all samples"

**Finding (owner, by ear, on the Phase 2 renders):** mouse clicks and breaths audible in every
processed file. **Measured cause** (10 ms RMS frames, ffmpeg decode, numpy):

| File | Speech RMS (p90) | Breath/room band | Expander threshold used | Effect of Phase 2 "Balanced" on the band |
|---|---|---|---|---|
| 1012am (mono, close mic) | −26.5 dBFS | −47 dBFS | −48 dBFS | −0.1 dB (nothing) |
| TOM0010669 (stereo, hot) | −7 dBFS | −28 dBFS | −48 dBFS | breaths 2 dB **louder** relative to speech (compressor) |

Breaths sit ≈ 21 dB under speech in both recordings, far above the spec's −48 dBFS starting
threshold, so the expander never engaged. Clicks measured with a 2 ms RMS detector: peaks
−26…−38 dBFS, 1–4 ms above a speech-relative threshold; the clicks in 1012am occur 50–100 ms
after a word, i.e. inside the 120 ms hold, so the Phase 2 gate was open when they happened.

**Changes made:** speech-relative threshold mode + analysis pass, transient rejection, shorter
hold/release and larger caps in the factory presets (see DSP_DESIGN.md). New automated tests:
analyzer (5 cases), relative threshold, click rejection, plosive preservation, engine latency /
bypass with the combined delay, file-processor and CLI `--analyze-only`.

**Measured result on the same files** (change vs. raw; "floor" = 10th percentile of 10 ms
frames, "breath band" = frames between floor+6 dB and speech−18 dB, clicks = peak in ±2 ms):

| Render | 1012am floor | 1012am breath band | 1012am clicks (2) | TOM floor | TOM breath band | TOM clicks (5) |
|---|---|---|---|---|---|---|
| Phase 2 Balanced | −0.3 dB | −0.1 dB | +0.3 / −0.1 dB | −2.6 dB | −4.0 dB | 0 … −2.5 dB |
| B2-gentle (Talk Radio tier) | −3.3 | −0.9 | +0.3 / −1.3 | −5.6 | −5.2 | −11…−12 |
| B2-medium (Balanced tier) | −8.8 | −2.2 | −3.7 / −6.3 | −13.1 | −6.4 | −20 |
| B2-strong (Noisy Room tier) | −19.1 | −11.8 | −13.3 / −17.2 | −23.7 | −11.3 | −30 |

Reading: in TOM every click is in a pause with the gate closed, so each tier attenuates them by
exactly its cap. In 1012am the two clicks follow words closely; only the strong tier (30 ms
hold, 50 ms release) closes fast enough to take 13–17 dB off them. Breaths in 1012am are only
4–6 dB under the medium tier's close point, so medium takes 2 dB off them and strong 12 dB; the
price of "strong" is that word tails 20–25 dB under the speech level are attenuated too
(capped at 30 dB). **This trade-off needs the owner's ears**, which is why three tiers were
rendered: `~/Desktop/RaDiff-Listening/B2-{gentle,medium,strong}-*.wav` against `A-raw-*.wav`
(Phase 2 renders moved to `old-phase2/`). Speech level (p90) is unchanged between tiers
(−0.5 dB on 1012am, −7.8 dB on TOM from compression), so the A/B is loudness-comparable per file.

Analysis pass on the three raw files (`RaDiffVoiceCLI --analyze-only`): speech level
−28.9 / −12.7 / −18.6 dBFS, noise floor −61.1 / −49.6 / −35.4 dBFS, resolved Balanced
threshold −40.9 / −24.7 / −30.6 dBFS (1012am / TOM / vikings). Release CLI speed with the
analysis pass: 46 s file in 0.32 s (144× real time; was 283× with a single pass). vikings is an already
processed broadcast file (15 dB dynamic range) and has no detectable breaths or clicks.

**Unprocessed reference file** (owner-supplied `TOM0010679.mp3`, 192 kb/s 48 kHz stereo, decoded
to 24-bit WAV; the earlier files had already been processed elsewhere). Peak +0.3 dBFS (MP3
overshoot), gated speech level −20.6 dBFS, breaths −37…−46 dBFS (9 segments of 150–450 ms, e.g.
8.60 s, 14.25 s, 19.80 s), digital silence between takes (−91 dBFS floor). Resolved Balanced
threshold −32.6 dBFS. Clicks at 27.87 s (−14.5 dBFS, inside a 150 ms gap in a sentence),
36.40 s (−23 dBFS, in a pause) and 36.86 s (−28 dBFS, 50 ms after a word).

| Render | floor | breath band (median −43.5) | click 27.87 s | click 36.40 s | click 36.86 s |
|---|---|---|---|---|---|
| B2-gentle | −12.4 dB | −6.0 dB | −3.6 | −11.8 | −2.2 |
| B2-medium | −20.4 | −12.5 | −6.1 | −19.9 | −7.8 |
| B2-strong | −30.4 | −24.4 | −16.9 | −30.1 | −18.4 |

Same pattern as before: a click in a real pause gets the full cap; a click inside the hold or
release right after a word gets only what the release has reached so far.

### Listening round 2 — owner verdict (2026-10-21)

Owner listened to `B2-gentle/medium/strong` against `A-raw-*` for all four files and chose
**medium** ("I feel like medium is a good setting"). Medium is the tier the Balanced, Bright,
Deep and Female presets already use (speech-relative −12 dB, ratio 3:1, hysteresis 3 dB,
hold 40 ms, release 60 ms, cap 20 dB, transient reject 12 ms, lookahead 15 ms), so the factory
defaults stay as committed in `bdb7ec7`; no preset was changed by this verdict. Known and
accepted at this tier: a mouse click that lands inside the hold/release right after a word is
reduced by 6–8 dB rather than the 20 dB cap (TOM0010679 at 27.87 s and 36.86 s). Gentle
(Talk Radio, Podcast, Soft Spoken) and strong (Noisy Room) remain available as presets.
Status: expander tuning **listening-approved for file mode at the medium tier**; the
"awaiting listening approval" wording in the preset descriptions is kept until the full chain
(Phase 6 stages) has been heard.

### Automated suites after the change

| Preset | Result |
|---|---|
| `mac-debug` | 33/33 CTest tests passed; `test_core` 59 cases / 4695 assertions |
| `mac-asan` (ASan + UBSan) | 33/33 passed, no sanitizer reports |
| `mac-universal-release` | 33/33 passed (arm64 slice executed; x86_64 slice built, not executed) |

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

See the round-1 section below. No factory preset is approved yet; descriptions say so.

### Not verified (and therefore not claimed)

Windows compilation and tests; x86_64 execution; any plug-in format; any DAW host; dither on
bit-depth reduction (not implemented yet); long-duration/stress runs.

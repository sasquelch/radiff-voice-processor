# DSP design

All processing is original C++20 in `src/core`, built from published, standard techniques.
Samples are 32-bit float at the interface; filters, detectors and gain computation run in
double precision. Algorithm version: **1** (`core::kAlgorithmVersion`).

## Signal order (v1, fixed)

```
input trim → DC blocker → high-pass → 5-band EQ → downward expander → de-esser → compressor 1
→ compressor 2 → presence/enhancer → output trim
```

Stages defined by the master specification but not yet implemented (each gets its own tests
before any factory preset enables it): multiband expander, saturation, output limiter.
`signalOrder` in presets is validated against the implemented chain (files that list fewer
modules, e.g. written before the de-esser existed, load as long as the modules they do list are
in this order); unknown future module names are accepted and ignored with a warning.

### Coverage of a hardware channel strip (dbx 286s class)

| Hardware section / control | RaDiff equivalent |
|---|---|
| Mic preamp gain, 80 Hz HPF | `input.gainDb` (±24 dB; the preamp itself is your interface), `input.highPassEnabled/Hz/Slope` |
| +48 V phantom, insert loop | hardware only, no software equivalent |
| Compressor Drive, Density, bypass, GR meter | `compressor1.thresholdDbfs` + `compressor1.autoMakeup` (Drive = lower threshold with gain compensation), `compressor1.releaseMs` / `autoRelease` (Density), `compressor1.enabled`, `Meters::compressor1ReductionDb`; plus ratio, knee, attack, detector, sidechain HPF, wet, link, and a second compressor |
| De-esser Frequency (800 Hz–10 kHz), Threshold, bypass | `deesser.frequencyHz` (800 Hz–12 kHz), `deesser.thresholdDbfs`, `deesser.enabled`; plus bandwidth, max reduction, attack/release, listen, stereo link |
| Enhancer LF Detail, HF Detail, bypass | `presence.lfDetail` (0–10), `presence.hfDetail` (0–10), `presence.enabled`; plus both corner frequencies, `hfDynamic` program dependence, makeup |
| Expander/Gate Threshold, Ratio, bypass | `expander.thresholdDbfs` or speech-relative mode, `expander.ratio`, `expander.enabled`; plus knee, attack/hold/release, hysteresis, lookahead, attenuation cap, transient rejection, wet, link |
| Output Gain, Process Bypass, output meter | `output.gainDb`, global `bypass`, `Meters::outputPeak` |

## Common building blocks (`core/dsp/`)

| Block | Design | Notes |
|---|---|---|
| `Biquad` | RBJ Audio-EQ-Cookbook designs (LP, HP, peaking, low/high shelf), transposed direct form II, double state | Coefficient changes ramp linearly over 5 ms, so parameter moves never click. Frequencies clamp to [1 Hz, 0.45·fs], Q to [0.05, 20]. |
| `DcBlocker` | `y = x − x₁ + R·y₁`, R for 5 Hz | Removes offset; < 0.01 dB effect at 100 Hz. |
| `LevelDetector` | RMS: one-pole mean-square, time constant = window. Peak: rectified envelope, instant attack, decay = window | Returns dBFS. A 0 dBFS-peak sine reads −3.01 dBFS RMS / 0 dBFS peak. |
| `GainBallistics` | One-pole smoothing of the gain in dB with separate attack/release coefficients, `coef = exp(−1/(t·fs))` | All times are **time constants** (63 % of a step after one `t`). |
| `GainComputer` | Quadratic soft-knee static curves (Giannoulis, Massberg & Reiss, JAES 2012) | Compressor slope `1/R−1` above threshold; expander slope `R−1` below threshold, capped at `−maxAttenuation`. |
| `OnePoleSmoother`, `EnableRamp` | 10 ms gain smoothing; 10 ms linear crossfade for module/band toggles | Zipper-free gain moves, click-free bypasses. |
| `DelayLine` | Integer delay, allocated in `prepare()` | Expander lookahead and full-chain bypass alignment. |
| `ScopedFlushDenormals` | FTZ/DAZ via MXCSR (x86-64) or FPCR.FZ (ARM64) for the duration of `process()` | Architecture guard, not platform guard. |

## Input stage

- Input trim −24…+24 dB (smoothed).
- DC blocker on/off (crossfaded).
- High-pass 20–250 Hz, 12 dB/oct (one Butterworth biquad, Q 0.7071) or 24 dB/oct (two cascaded
  biquads, Q 0.5412 / 1.3066). Measured: −3.0 dB at fc, −12.3 dB (12) / −24.1 dB (24) one octave
  below. Toggling crossfades; filter state is reset on re-enable so no stale energy pops.
- Channel-link semantics: both channels always receive identical filtering; channel linking
  is a property of the dynamics detectors (`stereoLink` on expander and compressors).

## Five-band EQ

Five cascaded biquads per channel; each band has enabled, type (bell, lowShelf, highShelf,
highPass, lowPass), frequency 20 Hz–20 kHz (clamped below Nyquist), gain ±18 dB, Q 0.2–12.
Disabled bands are skipped entirely (flat EQ is bit-transparent). Module and per-band toggles
crossfade over 10 ms. `Equalizer::magnitudeDb(hz)` returns the analytic response for the UI
curve and is cross-checked against measured responses in tests.

## Downward expander (speech preservation stage)

```
threshold: absolute (thresholdDbfs)  or  speechRelative: measured speech level + relativeThresholdDb
detector:  optional sidechain HPF/LPF → RMS or peak level (dBFS), per channel
           + a fixed 2 ms RMS "fast" detector used only for transient rejection
link:      stereoLink → max(L, R) drives one gain for both channels
state:     open when level ≥ threshold (and, with transientRejectMs > 0, the fast detector has
           been above the threshold for ≥ 2/3 of the last transientRejectMs);
           closed when level < threshold − hysteresis
target:    open   → curve(level, threshold − hysteresis)   (≈ 0 dB)
           hold   → 0 dB for holdMs after closing
           closed → curve(level, threshold) = (R−1)·(level−T) through a soft knee, floored at −maxAttenuationDb
           burst  → closed, but an unverified burst keeps curve(pre-burst level) until it has rung down
smooth:    opening uses attackMs, closing uses releaseMs (time constants)
apply:     gain × audio delayed by max(lookaheadMs, transientRejectMs); output = dry·(1−wet) + wet·processed
```

### Speech-relative threshold (added after the first listening round)

The spec's −48 dBFS is "a starting test value only". On the real voice tracks the breaths and
mouse clicks sat about 21 dB under the speech level, i.e. at −47 dBFS and −28 dBFS in two
recordings, both far **above** −48 dBFS, so the expander never touched them and the compressors
then raised them relative to speech. `expander.thresholdMode = "speechRelative"` fixes the
threshold `relativeThresholdDb` under the speech level of the material instead:

- `core::LevelAnalyzer` measures the recording from 10 ms RMS frames in the detector domain
  (after input stage and EQ, dynamics off). Speech level = energy mean of frames above two
  relative gates (ungated mean −20 dB removes pauses, then stage-1 mean −10 dB, the BS.1770-4
  relative gate, removes room tone and breaths). Tested: identical within 1 dB for 30 % and
  70 % speech duty, unchanged (< 0.5 dB) by breaths 20 dB under speech, block-size independent.
- The host passes the number with `Engine::setSpeechLevelDb()`; it is a property of the
  recording, so it is never stored in a preset. `app::processFile` always runs the analysis
  pass first and the CLI reports it (`--analyze-only`, `--verbose`, `--json-result`).
- Without a measurement (e.g. a realtime host before the standalone/plug-in get an online
  tracker) the absolute `thresholdDbfs` is used. The resolved threshold is reported.

### Transient rejection (mouse clicks in pauses)

Measured on the real files, a mouse click keeps a 2 ms RMS detector above a speech-relative
threshold for 1–4 ms, a word onset for its whole duration. With `transientRejectMs = 12` a
closed expander only opens after 8 ms of such evidence inside the last 12 ms, so a click cannot
open it (and hold cannot be re-armed by it), while a plosive burst followed by a vowel still
qualifies (tested). Because the audio is delayed by the same 12 ms, the opening is already
under way when a genuine onset reaches the output; `lookaheadMs` larger than the window adds
pre-roll for the attack. A burst that is rejected, or an open shorter than the window, keeps
the attenuation of the level that preceded it (a 20 ms slow-rising tracker) until the main
detector has rung down, so neither the click nor the room tone behind it "pops" through.
Rejection only acts while the expander is closed: clicks inside words or inside the hold/release
period after a word are **not** removed; that is the spec's "no magical click elimination".

### Key guarantees (all tested in `tests/core/test_expander.cpp`)

- **Attenuation never exceeds `maxAttenuationDb`**: a −75 dBFS floor with a 10 dB cap comes out
  at −85 dBFS, not muted.
- Hysteresis keeps a word that dips a few dB under the threshold open; hold keeps the gate open
  for word endings; attack opens within a few ms so onsets are not clipped; lookahead
  (0–20 ms) lets the gain open **before** the onset and is reported as latency.
- With `hysteresisDb = 0` the soft knee is continuous through the threshold.
- The delay is applied even when the module is disabled, so latency depends only on the
  lookahead / transient-reject parameters, never on enable state.
- With `transientRejectMs = 0` and `thresholdMode = absolute` the behaviour is bit-identical to
  algorithm v1 before these additions (steady-state and timing tests unchanged), so
  `kAlgorithmVersion` stays 1; the factory presets changed, the algorithm for existing presets did not.
- Detector type is a level detector, not a speech detector; the documentation and presets say so.

### Factory preset tiers (after listening round 1, awaiting approval)

| Tier | relativeThresholdDb | ratio | cap | hold / release | hysteresis | Presets |
|---|---|---|---|---|---|---|
| gentle | −14 (Soft Spoken: −16, 2:1, 8 dB) | 2.5 | 12 dB | 50 / 80 ms | 3 dB | Talk Radio – Natural, Podcast Voice, Soft Spoken Voice |
| medium | −12 (CHR: −11, 3.5:1, 24 dB) | 3 | 20 dB | 40 / 60 ms | 3 dB | Broadcast Voice – Balanced, Bright, Deep, Female, CHR |
| strong | −10 | 4 | 30 dB | 30 / 50 ms | 2 dB | Noisy Room – Conservative |

All tiers: knee 6 dB, attack 5 ms, RMS 10 ms, transient rejection 12 ms, lookahead 15 ms
(latency 15 ms). Minimal Processing keeps the expander off.

## Compressors 1 and 2

Identical feed-forward design, two instances with separate parameters (leveling, then density):

```
detector: optional sidechain HPF → RMS or peak level, per channel; stereoLink → max(L, R)
target:   compressorGainDb(level, threshold, ratio, knee)
smooth:   more reduction → attackMs, less reduction → releaseMs (time constants)
auto:     optional program-dependent release: release time × (1 + 3·clamp(avgGR/6 dB, 0, 1)),
          where avgGR is a 1 s average of the applied reduction (brief peaks recover quickly,
          sustained loud passages release up to 4× slower)
apply:    gain × makeup (smoothed); output = dry + wet·(processed − dry)
```

Measured in `tests/core/test_compressor.cpp`: static curve to ±0.01 dB, knee value
`(1/R−1)·W/8` at threshold, 63 %/37 % points at the attack/release time constants, linked vs
independent stereo, sidechain HPF, auto-release ordering, auto makeup.

**Auto makeup** (`autoMakeup`, off by default): adds exactly the static gain reduction that a
−20 dBFS RMS signal receives at the current threshold/ratio/knee, on top of the manual makeup.
A nominal-level voice therefore leaves at the level it entered when the threshold is lowered
(the way a hardware "Drive" control behaves). It is a fixed, visible number
(`Compressor::autoMakeupDb()`), not a level-dependent gain rider; tested: −20 in → −20 out,
−40 in → −35 out with threshold −30, 2:1.

## De-esser

```
band   = BPF(frequencyHz, Q from bandwidthOctaves)(x)      RBJ constant-0 dB-peak band-pass
level  = peak detector(band), 3 ms decay, dBFS; stereoLink → max(L, R)
reduce = min(maxReductionDb, 1:∞ limiting above thresholdDbfs with a 6 dB quadratic knee)
smooth = attackMs (more reduction) / releaseMs (less reduction) time constants
y      = x − (1 − g)·band                                     g = 10^(−reduce/20)
```

Only the detection band is reduced: at the centre frequency the band-pass has zero phase, so
the output there is exactly `g·x`; the reduction fades with the band-pass skirt and the rest of
the spectrum is untouched (tested: 6 kHz band −6 dB while a simultaneous 1 kHz tone moves
< 0.1 dB). Nothing is ever cut by more than `maxReductionDb` (0–12 dB, factory default 6).
`listen` outputs the detection band for tuning and is never written to or read from a preset
file. Off = bit-exact pass-through after a 10 ms crossfade. Tests: `tests/core/test_deesser.cpp`
(static curve, cap, timing, listen, stereo link, disabled). Factory presets ship with it OFF
(spec: enable lightly, e.g. 6 kHz / 3 dB, once recordings call for it).

## Presence / enhancer (LF Detail, HF Detail)

Plainly stated, this is tone shaping, not magic:

* **LF Detail** 0–10 → low shelf at `lfFrequencyHz` (default 100 Hz), +0.6 dB per step, Q 0.707.
* **HF Detail** 0–10 → high shelf at `hfFrequencyHz` (default 3.5 kHz), +0.8 dB per step, Q 0.707.
* **hfDynamic** 0–1 makes the HF shelf program dependent: the module input's level above
  `hfFrequencyHz` (2nd-order HPF, 10 ms RMS) is compared with its wideband level; when the high
  band already dominates (sibilance, mouth clicks, hiss) the boost is scaled down by up to
  `hfDynamic`×100 %, linearly over the last 12 dB of dominance. The shelf gain is smoothed
  (30 ms) and the filter re-designed every 32 samples with coefficient ramping (no zipper
  noise). With `hfDynamic` = 0 it is a static shelf. Detection taps the module input, so the
  boost never feeds its own detector. Limitation: a click shorter than the 10 ms detector still
  gets part of the boost, which is one reason the module is OFF in every factory preset.
* **makeupGainDb**: explicit compensating gain. There is no automatic loudness change.

Tests: `tests/core/test_presence.cpp` (shelf gains against the designed response, dominance
back-off to < 0.5 dB on a lone 10 kHz tone and full boost under a dominant low tone, half
sensitivity, makeup, bit-exact off, click-free enable).

## Metering

`Engine::meters()` returns per-channel input/output block peaks (linear) and the maximum
reduction (positive dB) applied by the expander, de-esser, compressor 1 and compressor 2 in the
last block. Reads are relaxed atomics; UI ballistics are the UI's job.

## Latency and bypass

`Engine::latencyFrames()` = max(expander lookahead, transient-reject window) in samples. Full-chain bypass passes the input
through an equal delay so bypassed audio stays time-aligned with processed audio and hosts
need no latency change on bypass. Offline rendering (`app::processFile`) flushes `latency`
frames of silence and shifts the result so output frame *i* corresponds to input frame *i*
and the frame count is unchanged (tested with impulses).

## Block-size and sample-rate behaviour

All state is per-sample; nothing depends on the block length. Tests compare block sizes
1, 64, 1000 and 4096 (≤ 1e-6 difference) and check 44.1/48/96 kHz consistency.

## Analysis (`core/Analysis.h`)

`LevelAnalyzer` is the offline/measurement counterpart of the detectors: allocation-free after
`prepare()`, fed block by block, histogram based (0.25 dB bins). It reports duration, sample
peak, gated speech level, noise floor (energy mean of the quietest 10 % of non-silent frames)
and active time. The standalone's file panel and `--analyze-only` show these numbers.

### Online speech-level tracking (realtime hosts)

File processing measures the whole file first (two passes). Live input and plug-ins cannot, so
`Engine::setSpeechTracking(true, windowSeconds)` runs the same gated measurement on the post-EQ
signal with exponential forgetting (`LevelAnalyzer::setForgetting`, default time constant
20 s). Every 100 ms, once at least 1 s of gated speech has been heard, the expander's
speech-relative threshold follows the tracked level; before that the host-supplied value or the
absolute threshold applies. Because the gating keeps pauses, breaths and room tone out of the
histogram, the tracked level does not sink during silence, and a parameter change never resets
it. Tested in `tests/core/test_engine.cpp`: −20 dBFS speech-like tone → threshold −36 within
3 s with `relativeThresholdDb` −16; a drop to −30 dBFS is followed within six time constants;
disabling tracking restores the host value. The forgetting makes the first second after a big
level change tolerant rather than instantaneous, which is intended: a shouted word should not
move the gate.

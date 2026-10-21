# DSP design

All processing is original C++20 in `src/core`, built from published, standard techniques.
Samples are 32-bit float at the interface; filters, detectors and gain computation run in
double precision. Algorithm version: **1** (`core::kAlgorithmVersion`).

## Signal order (v1, fixed)

```
input trim → DC blocker → high-pass → 5-band EQ → downward expander → compressor 1 → compressor 2 → output trim
```

Stages defined by the master specification but not yet implemented (added in Phase 6, each
with its own tests before it is enabled in any factory preset): multiband expander, de-esser,
presence, saturation, output limiter. `signalOrder` in presets is validated against the
implemented chain; unknown future module names are accepted and ignored with a warning.

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
detector: optional sidechain HPF/LPF → RMS or peak level (dBFS), per channel
link:     stereoLink → max(L, R) drives one gain for both channels
state:    open when level ≥ threshold; closed when level < threshold − hysteresis
target:   open   → curve(level, threshold − hysteresis)   (≈ 0 dB)
          hold   → 0 dB for holdMs after closing
          closed → curve(level, threshold)  = (R−1)·(level−T) through a soft knee, floored at −maxAttenuationDb
smooth:   opening uses attackMs, closing uses releaseMs (time constants)
apply:    gain × audio delayed by lookaheadMs; output = dry·(1−wet) + wet·processed
```

Key guarantees (all tested in `tests/core/test_expander.cpp`):

- **Attenuation never exceeds `maxAttenuationDb`** (10 dB in the balanced preset): a −75 dBFS
  floor comes out at −85 dBFS, not muted.
- Hysteresis keeps a word that dips a few dB under the threshold open; hold keeps the gate open
  for word endings; attack opens within a few ms so onsets are not clipped; lookahead (0–20 ms)
  lets the gain open **before** the onset and is reported as latency.
- With `hysteresisDb = 0` the soft knee is continuous through the threshold.
- The lookahead delay is applied even when the module is disabled, so latency depends only on
  the lookahead parameter, never on enable state.
- Detector type is a level detector, not a speech detector; the documentation and presets say so.

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
independent stereo, sidechain HPF, auto-release ordering. There is no automatic makeup gain.

## Metering

`Engine::meters()` returns per-channel input/output block peaks (linear) and the maximum
reduction (positive dB) applied by the expander, compressor 1 and compressor 2 in the last
block. Reads are relaxed atomics; UI ballistics are the UI's job.

## Latency and bypass

`Engine::latencyFrames()` = expander lookahead in samples. Full-chain bypass passes the input
through an equal delay so bypassed audio stays time-aligned with processed audio and hosts
need no latency change on bypass. Offline rendering (`app::processFile`) flushes `latency`
frames of silence and shifts the result so output frame *i* corresponds to input frame *i*
and the frame count is unchanged (tested with impulses).

## Block-size and sample-rate behaviour

All state is per-sample; nothing depends on the block length. Tests compare block sizes
1, 64, 1000 and 4096 (≤ 1e-6 difference) and check 44.1/48/96 kHz consistency.

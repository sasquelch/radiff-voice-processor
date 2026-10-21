# RaDiff Voice Processor — Master Development Specification

**Specification version:** 1.0 (consolidated, 2026-10-21)  
**Status:** Authoritative product and engineering requirements  
**Development environment:** Claude Code on macOS  
**Required shipping platforms:** macOS and Windows  
**Primary use case:** Fully automatic processing of prerecorded radio voice tracks, with identical audio processing available through a standalone desktop app and DAW plugins.

> **READ THIS FIRST, CLAUDE CODE:** This is the complete master specification. It supersedes any earlier Windows-only plan or short macOS amendment. The current repository may contain a placeholder DSP engine with bypass/input/output gain; inspect and preserve working code, then implement the missing processing and applications in this document. The absence of any earlier document is **not** a blocker. Do not treat this file as pseudocode, a request for another plan, or permission to stop after scaffolding. Make incremental, tested code changes and report what you have actually built and verified.

---

## 1. Product mission and boundaries

Build **RaDiff Voice Processor**, an original, professional-quality vocal/broadcast channel-strip product that:

1. Processes audio files independently on macOS and Windows via graphical and headless command-line applications.
2. Works as a **VST3 effect** in compatible macOS and Windows DAWs, explicitly including Adobe Audition and REAPER as target hosts.
3. Works as an **AUv2 effect** in macOS hosts, including Logic Pro as a target.
4. Uses **one shared, independently written C++ DSP engine** for every host and platform.
5. Uses **portable, editable, versioned JSON presets** created in standalone mode and loadable by the CLI and plugins.
6. Is appropriate for **unattended PlayoutONE WebVT** radio voice-track processing before optional final Stereo Tool processing.
7. Is practical to build and use without paid DSP frameworks, commercial plugins, monthly services, cloud access, or licensing servers.

The goal is *functional coverage comparable to a vocal channel strip* (such as general functions available in Voxengo Voxformer or a dbx 286s): expansion/gating, vocal compression, de-essing, EQ, voice presence, optional multiband processing and saturation. **Do not copy proprietary code, decompile plugins, clone branding/UI/assets, or claim a bit-perfect or sonically exact Voxformer reproduction.** Develop the DSP independently using standard published DSP techniques.

### Explicit out of scope for v1

- Training or bundling ML speech-separation models, cloud speech cleanup, aggressive automated breath deletion, or magical click elimination.
- Editing the user's production PlayoutONE configuration without specific authorization and a rollback plan.
- Replacing a full DAW or supporting multitrack recording/editing.
- VST2, AAX/Pro Tools, and Linux plugin/GUI distribution (portable DSP/CLI design should not block future Linux builds).
- Product activation, subscriptions, or online services.

## 2. Primary audio-quality philosophy

**Preserve every word first; clean up the recording second; create a punchy radio sound third.** The prior workflow used REAPER with ReaGate and iZotope RX 11 Breath Control / Dialogue Isolate, followed by Stereo Tool. Aggressive breath-removal and click-removal experiments caused unnatural speech, lost quiet words, or damaged consonants. Those regressions must not be repeated.

Required audible behavior:

- Quiet speech, endings of words, consonants, brief pauses, breath onsets and natural vocal texture must survive processing.
- No default hard gate or complete silence between words. Use a downward expander with a **hard limit on maximum attenuation**, initially 10 dB for the balanced preset.
- Do not hide excessive AGC/automatic makeup gain in the background, or let gain rise aggressively in pauses.
- Do not make claims of removing a mouse click that overlaps speech without tradeoffs. Limited conventional noise attenuation is acceptable; damaging intelligibility is not.
- No pumping, gate chatter, unnatural sibilance, metallic speech, clipping, zipper noise, or spurious end-of-file truncation.
- No mandatory loudness normalization. Many recordings will pass through **Stereo Tool afterward**; allow headroom.
- All modules are bypassable. Provide genuine dry/wet or pre/post comparisons where applicable.
- A single global preset must work reasonably with different announcers, but adjustable settings and additional station/voice presets are essential. Do not promise a universal threshold that perfectly separates all noise and soft speech.

### Listening-based acceptance

Use multiple real, appropriately licensed or self-recorded examples: low-volume speech, abrupt word endings, whisper-like speech, loud delivery, different microphones, room noise, breaths, clicks, plosives, sibilance, silence and full breaks. Match playback loudness when comparing. **Measurements and unit tests do not substitute for a human listening test.** Treat preservation of intelligibility as a release gate. Record subjective findings and unresolved artifacts in `docs/TEST_RESULTS.md`.

## 3. Shipping matrix

| Product | macOS Apple Silicon ARM64 | macOS Intel x86_64 | Windows x64 |
|---|---|---|---|
| Native standalone graphical application | Required | Required | Required |
| Headless command-line file processor | Required | Required | Required |
| VST3 plugin | Required | Required | Required |
| Audio Unit AUv2 plugin | Required | Required | Not applicable |
| Same versioned JSON presets | Required | Required | Required |

Prefer **macOS Universal 2 binaries** for app, CLI, VST3 and AU, subject to actual build/dependency support. If universal builds are impossible, ship verified architecture-specific builds, document why, and do not describe them as universal. Choose a documented minimum macOS version supported by actual dependencies; do not silently raise it.

On Windows, target Windows 10/11 x64 with MSVC. On Mac, develop with Xcode/Apple Clang. Explicitly test actual Adobe Audition and REAPER builds **when those hosts are available**. VST3 validator and `auval` passing are separate evidence from hands-on host verification.

## 4. Technical stack, portability and licensing

- **C++20**; a single portable source tree.
- **CMake** and optional Ninja for builds; no IDE-only project as the sole build route.
- **iPlug2** for graphical standalone/plugin wrappers if it passes a real dependency/license audit, with **Steinberg VST3 SDK** for VST3 support. Prefer AUv2 through supported iPlug2/macOS facilities.
- A **framework-independent core DSP library**: no GUI, OS-specific I/O, plugin SDK, audio device, or DAW dependencies inside the DSP core.
- A **framework-independent headless CLI**: GUI is never initialized in command-line mode, and no audio playback hardware is necessary.
- Use a permissively licensed audio-file library (e.g. libsndfile if license obligations are checked) or an original correct WAV reader/writer. Include direct and transitive licensing checks; no unsupported assumption that every transitive component shares its wrapper's license.
- Use a small permissive JSON library if appropriate. Pin versions and record attribution/notice obligations.
- No JUCE, proprietary DSP engines, commercial plugin binary dependencies, GPL-only components, paid frameworks, or SaaS in required production paths without express approval.
- Store documentation and third-party license notices in repository. The requirement is **no licensing fees**, not **no open-source licenses**; maintain all notices and conditions.
- Local unsigned development must be possible without an Apple Developer membership. Signing/notarization and Windows code signing are optional release enhancements; note potential OS security prompts for unsigned downloads.
- No user secrets or Stereo Tool license keys in normal or verbose logs.

Use `std::filesystem` thoughtfully, support UTF-8/Unicode filenames, avoid assumed slash direction, drive letters, case-insensitive lookup, fixed user directories, audio device presence, or shell commands in the shared core. Windows specifics reside in Windows-only wrappers/scripts; Apple-specific code likewise remains at boundaries.

### One engine, multiple wrappers

Suggested structure (adapt to existing repo rather than blindly restarting it):

```text
RaDiffVoiceProcessor/
  CMakeLists.txt
  CLAUDE.md
  MASTER_DEVELOPMENT_SPEC.md
  README.md
  THIRD_PARTY_LICENSES.md
  cmake/
  docs/
    ARCHITECTURE.md
    DSP_DESIGN.md
    DEVELOPMENT_STATUS.md
    TEST_RESULTS.md
    RELEASE_CHECKLIST.md
  src/
    dsp/             # dependency-free audio algorithms
    common/          # shared types, parameters and services
    presets/         # JSON schema, versioning, migrations
    audioio/         # file reading, writing and verification
    cli/             # headless file-to-file executable
    app/             # GUI standalone app and preview
    ui/              # reusable knob/meter/panel definitions
    plugin/          # VST3/AU wrapper(s)
    platform/        # narrowly scoped macOS/Windows utilities
  presets/factory/
  scripts/
    windows-playoutone/
  tests/
    dsp/
    preset/
    fileio/
    integration/
    regression/
  .github/workflows/
  packaging/
```

A conceptual engine API (refine as needed):

```cpp
prepare(sampleRate, channelCount, maxBlockSize);
setParameters(snapshot);          // safely snapshot/change with smoothing
processBlock(inputBuffers, outputBuffers, frameCount);
reset();
flush();
getLatencySamples();
getMeters();
```

The audio callback must not perform file/network I/O, show dialogs, await locks, or allocate unbounded memory. Handle plugin instance independence, buffer-size changes, parameter automation, reset, latency, denormals, NaNs, and audio-thread safety. No hidden global processing state.

## 5. Canonical DSP chain

**Default v1 signal order** (all but fundamental gain stages independently bypassable):

1. Input trim + input measurement, optional DC-blocker and HPF.
2. Five-band EQ, flat by default; user configurable.
3. Downward expander (key speech-preservation component).
4. Optional multiband expander (OFF in balanced preset).
5. Frequency-selective de-esser.
6. Leveling compressor 1.
7. Character/density compressor 2 (OFF in balanced preset; available for punchy presets).
8. Presence enhancement (subtle or OFF initially).
9. Optional oversampled harmonic saturation (OFF initially).
10. Optional output peak limiter and final trim/metering.

If advanced configurable ordering is offered, start with predefined legal permutations; never allow non-sensical or unstable routing. Independent per-module bypass and reliable full-chain bypass are mandatory. An automated test must verify bypassed processing returns correctly time-aligned source samples, subject only to intentionally enabled input/output trims.

### Parameter contract

The values below are **required editable parameters**. Numeric ranges are implementation targets, not claims of optimal settings for every voice. Provide reasonable safety limits, explicit units, and fine/coarse UI adjustment. Preserve all parameter values in external JSON and DAW project state.

| Module | Parameters (minimum) | Range/units or v1 guidance |
|---|---|---|
| Input | enabled DC blocker, input gain, HPF enabled, HPF cutoff, slope, channel link | gain −24 to +24 dB; HPF 20–250 Hz, default 80 Hz; 12/24 dB/oct where feasible |
| Five-band EQ | band enabled, type, frequency, gain, Q | 5 bands; frequency 20 Hz–20 kHz (limited to below Nyquist); gain −18 to +18 dB; Q ~0.2–12; types bell, low/high shelf and where technically appropriate HP/LP |
| Expander | enabled, threshold, ratio, knee, attack, hold, release, hysteresis, detector type, RMS window, sidechain HP/LP, lookahead, maximum attenuation, wet/dry, linked stereo detector | threshold −80–0 dBFS; ratio 1:1–8:1; knee 0–18 dB; attack 0.1–100 ms; hold 0–1000 ms; release 10–2000 ms; hysteresis 0–12 dB; lookahead 0–20 ms; max attenuation 0–60 dB (balanced default 10); RMS/peak detector; conservative by default |
| Compressor 1/2 | enabled, threshold, ratio, knee, attack, release, makeup, detector type, RMS window, sidechain HPF, optional auto release, optional wet/dry, stereo link | threshold −60–0 dBFS; ratio 1–20:1; knee 0–18 dB; attack 0.1–100 ms; release 10–2000 ms; makeup −12 to +24 dB; no uncontrolled auto-gain |
| De-esser | enabled, detection frequency, bandwidth, threshold, maximum reduction, attack, release, detection monitor, stereo link | frequency ~2–12 kHz subject to Nyquist; max attenuation 0–12 dB; band-limited or split-band; natural output is priority |
| Multiband expander | enabled, 3- or 4-band crossover configuration, band thresholds, band ratios, attack/release, maximum attenuation per band, stereo link | OFF in balanced preset; stable complementary crossovers; explicit summation/phase tests |
| Presence | enabled, center/range, amount, optional dynamic sensitivity, compensating gain | subtle and OFF by default until objectively tested; avoid amplifying clicks/mouth noise |
| Saturation | enabled, drive, character, wet/dry, compensating output, oversampling factor | OFF by default; anti-aliasing verified at 44.1/48/96 kHz; avoid hidden loudness boosts |
| Output | output trim, limiter enabled, ceiling, optional lookahead/release, optional loudness and true-peak analysis | output trim −24 to +24 dB; limiter OFF or conservative by default; default headroom for Stereo Tool; never normalize implicitly |
| Global | input/output meters, module bypass states, preset metadata, deterministic signal order, oversampling/quality, latency reporting | stable behavior over different buffer sizes, no sample drops |

Parameters must be automatable where practical in VST3/AU. Design robust parameter IDs so adding modules does not break existing sessions. Version the processing algorithm if changes alter the audible transfer function.

### Module behavior details

**Input filters/EQ:** Use numerically stable filters (e.g. biquad implementations with sensible coefficient interpolation, or other published standard designs). Avoid impulse pops when toggling stages or changing coefficients. Define channel-link semantics and handle mono and stereo independently without changing stereo image unexpectedly.

**Expander:** Downward expansion occurs below threshold and must never exceed the configured attenuation limit. Implement soft knee, time constants, hysteresis and hold to avoid chopping words. A default 10 dB cap reduces unwanted sounds but does not mute them. Do not label a purely level-based detector as a guaranteed speech detector. Provide gain-reduction metering and an option to audition a detector signal/residual for diagnosis if technically feasible. Detector filtering and stereo linking must be testable.

**Compressors:** Provide transparent RMS-style leveling and a second capable of denser radio presentation. Support program-dependent release only if properly implemented and tested; it must be optional. Avoid over-boosting between sentences. Show per-stage gain reduction and output metering. Document detector timing and ratio/knee curves.

**De-esser:** Prefer split-band or frequency-sensitive gain management that attenuates sibilance without dulling whole speech. Preserve `s`, `sh`, `t`, and low-level word endings. Make the detection region audible via optional monitor mode, but never persist monitor mode as a dangerous on-air default.

**Multiband expansion:** Start disabled. Confirm that filter-bank recombination is stable, does not create unacceptable coloration or phase errors, and does not exaggerate consonant loss. Band-specific max-attenuation is mandatory.

**Presence/saturation:** Avoid speculative magic algorithms. Clearly document what the independently implemented DSP actually does. Saturation requires meaningful aliasing tests at supported sample rates. Enable oversampling only when it materially improves quality and account for filter latency.

**Limiter:** Define sample-peak vs true-peak behavior accurately. If v1 lacks true-peak limiting, label a sample-peak limiter accordingly rather than implying true-peak compliance. Preserve audible transients and avoid unnecessary loudness maximization.

### v1 balanced preset starting values (for testing, NOT a universal solution)

- Input gain: 0 dB; DC blocker ON; HPF ON at 80 Hz; EQ flat.
- Expander: ON, threshold −48 dBFS **as a starting test value only**, ratio 2:1, knee 6 dB, attack 5 ms, hold 120 ms, release 250 ms, hysteresis 4 dB, maximum attenuation 10 dB, RMS detection, wet 100%; adapt threshold to each recording's real quiet speech/noise floor.
- Multiband expander: OFF.
- De-esser: OFF initially until tuned; later enable lightly (e.g. ~6 kHz center, maximum 3 dB reduction) when recordings require it.
- Compressor 1: ON, threshold −24 dBFS, ratio 2.5:1, knee 6 dB, attack 10 ms, release 150 ms, makeup gain 0 dB. Adjust after measuring voice levels.
- Compressor 2: OFF.
- Presence, saturation, limiter: OFF to begin.
- Output gain: 0 dB; preserve headroom. Do not claim these defaults are final mastered presets before listening tests.

## 6. Sound presets and serialization

Create the following **original** factory starting presets (not copied from competing processors):

1. Broadcast Voice — Balanced
2. CHR Radio — Punchy
3. Talk Radio — Natural
4. Soft Spoken Voice
5. Deep Voice
6. Bright Voice
7. Female Broadcast Voice
8. Podcast Voice
9. Minimal Processing
10. Noisy Room — Conservative

Use `.radvoice.json` as the file extension and explicit schema/algorithm versions. External sonic presets must not include machine-dependent input/output file paths. The application can maintain separate preferences for recent files, batch output folders, UI layout and audio devices.

Minimum example structure (evolve fields carefully and supply schema validation/migrations):

```json
{
  "schemaVersion": 1,
  "engineVersion": "1.0.0",
  "name": "Broadcast Voice - Balanced",
  "description": "Conservative speech-preserving starting preset",
  "author": "RaDiff",
  "signalOrder": ["input", "eq", "expander", "multibandExpander", "deesser", "compressor1", "compressor2", "presence", "saturation", "limiter", "output"],
  "modules": {
    "input": {"gainDb": 0, "dcBlockEnabled": true, "highPassEnabled": true, "highPassHz": 80, "highPassSlopeDbOct": 12},
    "eq": {"enabled": true, "bands": [
      {"enabled": false, "type": "lowShelf", "frequencyHz": 120, "gainDb": 0, "q": 0.7},
      {"enabled": false, "type": "bell", "frequencyHz": 300, "gainDb": 0, "q": 1},
      {"enabled": false, "type": "bell", "frequencyHz": 1200, "gainDb": 0, "q": 1},
      {"enabled": false, "type": "bell", "frequencyHz": 3500, "gainDb": 0, "q": 1},
      {"enabled": false, "type": "highShelf", "frequencyHz": 10000, "gainDb": 0, "q": 0.7}
    ]},
    "expander": {"enabled": true, "thresholdDbfs": -48, "ratio": 2, "kneeDb": 6, "attackMs": 5, "holdMs": 120, "releaseMs": 250, "hysteresisDb": 4, "detector": "rms", "rmsWindowMs": 10, "lookaheadMs": 0, "maxAttenuationDb": 10, "wet": 1, "stereoLink": true},
    "multibandExpander": {"enabled": false},
    "deesser": {"enabled": false},
    "compressor1": {"enabled": true, "thresholdDbfs": -24, "ratio": 2.5, "kneeDb": 6, "attackMs": 10, "releaseMs": 150, "makeupGainDb": 0, "detector": "rms", "stereoLink": true},
    "compressor2": {"enabled": false},
    "presence": {"enabled": false},
    "saturation": {"enabled": false},
    "limiter": {"enabled": false}
  },
  "output": {"gainDb": 0}
}
```

**Important:** The JSON above illustrates the structure, not permission to omit the full parameter contract. Define missing optional/default fields consistently in a formal schema; include all published parameters in serialization or document unambiguously which defaulted fields are implied. Preserve future preset compatibility through migrations rather than silent reinterpretation. DAW state must embed all sonic parameters so a DAW project reopens correctly when the external preset file is moved/deleted.

Preset functionality must include New, Load, Save, Save As, Duplicate, Rename, Delete user preset, Import/Export, factory reset, dirty/unsaved state, and schema validation. The same file must load on Mac GUI/CLI/VST3/AU and Windows GUI/CLI/VST3.

## 7. Standalone graphical application

The GUI must be a usable product, **not** a generic empty window or mockup. Use original design with familiar professional channel-strip affordances.

- Main window: preset selection/save, module enable controls, ordering if supported, input/output peak meters, compression/expander reduction meters, state indicators, current input/output files, processing status, warnings, and help/tooltips.
- Each DSP module has an intelligible parameter panel. Use correct dB, dBFS, Hz, ms, ratio and Q units; controls display current exact values.
- EQ has a response curve; compressor/expander optionally have input/output transfer curves. Responsive and high-DPI on macOS Retina and Windows.
- File panel: browse/open one WAV, choose output WAV and encoding, process, output progress and errors, show analysis/metrics. Drag/drop on Mac and Windows where supported.
- Preview: play/pause/stop/seek, waveform, original vs processed A/B at matched perceived loudness, optional quick preview render/caching, avoid GUI freezes. Allow previewing settings before saving.
- Batch: select several files, choose output directory, progress for each, retry failures, cancellation, collision policies, log and no accidental overwrite of sources.
- Standard file dialogs, keyboard accessibility, high-DPI scaling, clear error messages, user-preset folder management. Audio preview may require an output device but **offline processing must not**.
- Core DSP/preset system must be identical to CLI/plugin—not a secondary implementation in the UI layer.

## 8. Command-line audio processor

Build `RaDiffVoiceCLI` on macOS and `RaDiffVoiceCLI.exe` on Windows. Both must be executable without a DAW, GUI process, or installed audio interface.

Required syntax:

```bash
RaDiffVoiceCLI --input "/path/in.wav" --output "/path/out.wav" --preset "/path/Voice.radvoice.json"
```

Supported options (stable, documented):

- `--input`, `--output`, `--preset` (required for normal process mode)
- `--format`, `--sample-rate`, `--channels`, `--bit-depth`
- `--overwrite` (explicit opt-in; never overwrite source file)
- `--analyze-only`, `--log`, `--verbose`, `--json-result`, `--help`, `--version`

Support RIFF/WAVE, at minimum mono/stereo 16-, 24-, 32-bit integer PCM and 32-bit float, at sample rates 44.1, 48 and 96 kHz. Preserve input channels, sample rate and sample count by default. Additional codecs optional after these are stable. If reducing to integer PCM, implement and test appropriate dither rather than accidental truncation; specify output format explicitly when requested.

Processing and safety requirements:

1. Validate paths, file headers, channel/layout/sample-rate support, preset schema and parameter ranges before processing.
2. Initialize a fresh, fully independent DSP engine per job, so multiple concurrent CLI processes cannot bleed state.
3. Process offline in blocks without wall-clock sleeping; faster than real time on typical hardware where practical.
4. Compensate reported DSP lookahead/latency to retain the original frame count and correct alignment. Process/flush enough samples to avoid clipping tail words; document any effects with true tails.
5. Write to a unique temporary file **on the destination filesystem**. Fully close and validate it before publication.
6. Validate WAV structure, expected sample count/rate/channels, finite sample values, unexpected silence and peaks, taking into account legitimately silent source material. Don't flag valid silent inputs as failure simply because they're silent.
7. Atomically publish the completed output where supported; on replacement use a safe, documented method and `--overwrite`. Never expose incomplete output files.
8. Return **0 only on verified success**. Distinguish usage/invalid input, invalid preset, DSP, I/O, validation, cancellation/timeout and internal failures with documented nonzero codes.
9. Logs may contain source/destination names, timings, statistics and warnings, but never secrets, encoded plugin data dumps, or a Stereo Tool license key.
10. Avoid unnecessary format conversion, hidden loudness maximization, extra leading silence or truncation.

No user prompts. Support Unicode Windows paths with spaces and long paths where platform APIs permit. Cancellation and errors must clean temporary files or quarantine them safely; preserve originals.

## 9. DAW plugin behavior

Deliver **VST3 on Windows/macOS** and **AUv2 on macOS** using the same compiled DSP module behavior and parameter registry.

- Mono and stereo audio effect buses; stable reconfiguration upon sample-rate, buffer-size, bus-layout and transport changes.
- Audio callback free of blocking I/O, unnecessary allocations, UI calls, synchronous locks or GUI dependencies.
- Correct host automation, stable parameter IDs, sample-appropriate smoothing, bypass, state restoration, latency report and editor open/close behavior.
- DAW session saved state remains self-contained independent of external preset availability; importing/exporting `.radvoice.json` uses same schema and algorithms as CLI.
- Real-time and offline renders must sound consistent under identical inputs/settings and initial state. Independent instances do not share state.
- Plugin UI should look and feel similar to standalone controls; host, not plugin, owns track-level audio files.
- Run Steinberg VST3 validator and macOS `auval`. Where available, load into Adobe Audition (Mac/Win), REAPER (Mac/Win), and Logic Pro (AU on Mac), and process/play back an actual test file. Record **tested vs merely targeted** explicitly.
- No claim of AAX or VST2 compatibility.

## 10. PlayoutONE WebVT integration (Windows)

The existing Windows radio workflow:

```text
PlayoutONE WebVT browser recording
  -> PlayoutONE external process / existing batch wrapper
  -> REAPER + RX 11 effects (to be replaced)
  -> Stereo Tool command-line processor
  -> final PlayoutONE recording import
```

Desired workflow:

```text
PlayoutONE WebVT
  -> RaDiffVoiceCLI.exe with saved station preset
  -> optional EXISTING stereo_tool_cmd.exe with existing .sts preset
  -> PlayoutONE final output
```

Previously observed positional arguments in user's PlayoutONE processing wrapper:

- `%~1`: input WAV path
- `%~2`: final output WAV path
- `%~4`: Stereo Tool `.sts` preset path
- `%~6`: Stereo Tool license key

These are **the current wrapper's mappings**, not a claim about all PlayoutONE versions. Confirm actual arguments, file formats, import timing/metadata and watchdog behavior on a **test** configuration before production changes.

Build a separately deployable Windows `cmd`/PowerShell wrapper that:

- Runs the RaDiff CLI against `%~1` with a configured `.radvoice.json` preset, writing an isolated temporary intermediate WAV.
- Checks nonzero exits, output existence, header, duration/sample-count and finite sample data.
- Optionally invokes existing `C:\PlayoutONE\Modules\stereo_tool_cmd.exe` with the intermediate WAV, `%~2`, preset `%~4` and license `%~6` (or securely configured equivalent) without echoing the key in logs.
- Stages and publishes final output safely wherever PlayoutONE permits; do not make assumptions about whether PlayoutONE itself manages the destination.
- Uses collision-resistant job IDs and handles concurrent imports; avoids race conditions/partial-read issues.
- Preserves original inputs and logs errors. Failure must never be silently reported as success or quietly substitute a raw voice track without an explicitly configured fallback.
- Provides documentation for install, test, failover and immediate rollback to the current REAPER or Stereo Tool workflow.
- Does **not** modify the current production batch script or paths automatically.

The RaDiff application itself must be fully independent of REAPER, Stereo Tool and PlayoutONE. The chain is an optional business integration, not a runtime dependency.

## 11. Development machine and cross-platform builds

**Develop on macOS using Claude Code.** Start by detecting the current system: macOS version, CPU architecture, Xcode/toolchain, cmake, Git, iPlug2/SDK checkout, available DAW hosts. Build and run native Mac DSP tests and CLI there.

**Windows must build early**, not as the last release step. Use a native Windows x64 MSVC runner in GitHub Actions or an equivalent available Windows machine. Create macOS and Windows CMake presets and CI jobs; use pinned dependencies and stable build commands. Do not claim that macOS compilation verifies Windows portability.

GitHub Actions/hosted CI can have quotas or billing restrictions; never make a paid service mandatory. Document manual local Windows build steps, and if CI is blocked, distinguish "workflow authored" from "workflow executed and green". Avoid pretending unavailable Windows DAW tests passed.

Build matrix:

- macOS ARM64 Debug/Release, tests and native CLI.
- macOS Intel x86_64 build and tests when hardware/runner exists; otherwise Universal2 build validation and document what couldn't be executed natively.
- macOS VST3/AUv2 plugin validation.
- Windows x64 MSVC Debug/Release, CLI tests and VST3 validator.
- Host smoke testing when the specific host is present.

Release packaging: native `.app`, `.vst3`, `.component`, CLI binaries, Windows `.exe`/VST3 bundles, factory presets, license notices, portable archives and installation instructions. Optional macOS signing/notarization/Windows code signing supported but not required for private local testing.

## 12. Test plan and measurable release gates

### Deterministic core/unit tests

- Pass-through is transparent at unity (within documented float tolerances) and sample-aligned.
- Input/output gain dB-to-linear correctness.
- HPF, EQ frequency responses and filter stability, including frequency/Q extremes and Nyquist handling.
- Expander transfer curves, attack/hold/release/hysteresis, knee, maximum attenuation guarantee, and threshold behavior.
- Compressor static ratio/knee curves, release/attack response, makeup gain, and stereo detector linking.
- De-esser frequency sensitivity and bounded sibilance attenuation.
- Multiband sum/phase response, no invalid crossover combinations and stable recombination.
- Limiter ceiling semantics, no NaN/Inf, saturation aliasing checks.
- Preset validation, missing/unknown fields, round trips, schema migration and consistent module parameter IDs.
- Block-size independence within numerical tolerance and correctness across 44.1/48/96 kHz, mono/stereo, silence and abrupt changes.
- Accurate reported latency and offline compensation. Ensure source audio frames match output frames by default.
- No crashes, runaway memory or denormal CPU spikes under prolonged/repeated batch runs.

### File/CLI integration

- Round-trip PCM16/24/32, float32, mono/stereo WAV, filenames with Unicode/spaces, very short and multi-minute clips.
- Invalid/malformed/truncated RIFF and unsupported format produce clear nonzero exits.
- Unwritable destinations, read-only files, disk failure simulation, collisions, concurrency and interrupted publication must not corrupt originals or publish partial finals.
- Correct output bit depth, rate, duration and channels; appropriate dither when reducing precision.
- Normal **silent input** is handled safely; unexpected all-silence output on speech-containing test inputs is detected.
- Headless operation requires neither GUI nor audio hardware.

### Listening/regression tests

Produce test WAVs and manual scorecards for: soft words, consonants, sentence endings, breath sounds, keyboard/mouse clicks, constant noise, loud compressed announcer, different mic tones, plosives, sibilance, pauses. Compare (A) raw, (B) RaDiff alone, and (C) RaDiff → Stereo Tool using the same final Stereo Tool settings. Loudness-match A/B. Document any speech damage. No destructive cleanup should be enabled in default presets without listening evidence.

### Cross-edition/host tests

Run a common audio test file through Mac CLI and Windows CLI, Mac standalone and Windows standalone, VST3 offline render(s) and AU offline render. Compare aligned output with sensible floating-point tolerances (not unreasonably demand bit-perfect identity between CPU architectures). Run VST3 validator and `auval`. Verify DAW reopen, automation and preset portability. Record actual environments/versions used.

## 13. Milestones and acceptance criteria

**Phase 1 — Foundation and portable placeholder (may already exist)**
- Mac CMake build works, dependency/license audit started, source structure, shared parameter registry and tests available, placeholder pass-through/gain engine compiles.
- If current repo already has this, **do not restart the repo or waste time rebuilding it**. Verify and continue.

**Phase 2 — REAL DSP engine (begin here if current engine is placeholder)**
- Implement working input filters, five-band EQ, speech-preserving expander, compressor 1 and compressor 2; no no-op placeholders.
- All named parameters with sensible unit/range validation, bounded gain, smoothing and meters.
- Implement tests measuring frequency/level/dynamic transfer and latency/block-size behavior. Audition test recordings.
- A proof-of-function output must demonstrably change sound when processing is active and preserve raw sample timing when bypassed.

**Phase 3 — CLI, WAV I/O and presets**
- Implement full `.radvoice.json` schema, first factory presets, real audio input/output and validation.
- `RaDiffVoiceCLI --input ... --output ... --preset ...` successfully processes sample WAVs on Mac without DAW, audio device or plugin dependencies.
- Start Windows native compilation and tests early in this phase (preferably sooner).

**Phase 4 — Standalone GUI on both platforms**
- Native file pickers, working module controls, meters, presets, preview, A/B and batch queue; actually process files on Mac.
- Windows standalone compiles and its file-processing tests run on Windows.

**Phase 5 — VST3/AUv2 builds and host validation**
- VST3 Mac/Win, AU Mac. Validate plugin and DAW state round trips. Test available hosts without overstating untested compatibility.

**Phase 6 — Advanced processing and tuning**
- De-esser, multiband expander, presence, optional saturation, limiter; verify quality and disable unproven stages in conservative factory presets.

**Phase 7 — Broadcast automation integration**
- Tested Windows wrapper, optional Stereo Tool handoff, concurrency/error/failover plan, comparison to legacy REAPER chain on a staging system.

**Phase 8 — Release**
- Clean release builds, host tests where available, license notices, packaging, docs, installer/archives, performance/stress/listening results, known limitations and deployment/rollback instructions.

**Definition of done:** Running applications and plugins, real functioning DSP, proper preset interchange, successful automation of recorded WAV files, validated cross-platform builds, test results and documentation. Build success alone is not product completion. Mark items "untested" if their platform/host isn't available rather than falsely claiming verification.

## 14. Documentation and working conventions for Claude Code

Maintain or update:

- `CLAUDE.md` — instructions and link to this authoritative spec, build commands, currently active milestone, machine-specific notes.
- `docs/ARCHITECTURE.md` — architecture and dependency decisions.
- `docs/DSP_DESIGN.md` — algorithms, filter/detector choices, parameter descriptions, phase/latency semantics.
- `docs/DEVELOPMENT_STATUS.md` — verified accomplishments, next steps, known blockers.
- `docs/TEST_RESULTS.md` — executed build/test/host/listening results with dates and command lines.
- `docs/RELEASE_CHECKLIST.md` — packaging, QA, license and verification matrix.
- `THIRD_PARTY_LICENSES.md` — exact pinned dependencies and notice obligations.

Use version control; commit incremental, tested progress where permitted. Favor correct working features over invented marketing claims. Do not introduce giant unexplained code dumps, incompatible preset changes or destructive modifications to the user's existing working broadcast setup. Make sensible implementation decisions without requesting permission for routine choices, but ask before introducing paid/recurring dependencies or altering production files.

### Required initial continuation actions

1. Read **this full specification** and `CLAUDE.md`, then inspect the existing repository and tests. Summarize the actual current state in a few sentences.
2. **Do not ask for the missing master spec again**: this file is that spec, including prior Mac/Windows changes.
3. Determine whether only bypass/input/output gains are implemented. If yes, start **Phase 2** by implementing a real, tested high-pass/DC filter, then parametric EQ, then capped downward expander, then compressors.
4. For each DSP stage: implement it, add deterministic unit tests, verify no regressions and report measured behavior before moving on.
5. Preserve existing Mac builds and keep Windows compatibility checks active. Do not rewrite working files unnecessarily.
6. Continue through phases 3–8 toward a real standalone application, CLI and plugins, documenting blockers accurately rather than substituting plans for implementation.

**Final priority:** Excellent natural speech and robust unattended file processing outweigh extra knobs, flashy interfaces or unproven automatic noise removal.

# Standalone application (Phase 4)

`RaDiffVoice.app` (macOS) / `RaDiffVoice.exe` (Windows) is the iPlug2 **APP** build of the shared
engine: the same `radiff::core::Engine`, the same `.radvoice.json` presets and the same
`app::processFile()` job as the CLI. Nothing sonic is re-implemented in the UI layer.

## Build and run (macOS, verified)

```bash
cmake --preset mac-debug            # RADIFF_BUILD_STANDALONE defaults to ON on macOS/Windows
cmake --build --preset mac-debug --target RaDiffVoice-app
open build/mac-debug/out/RaDiffVoice.app
```

The bundle contains the factory presets (`Contents/Resources/presets`, copied from
`presets/factory` at build time) and the UI font. Debug builds also fall back to the source tree's
`presets/factory` when started from the build directory. Audio device, buffer size and sample rate
are chosen in *RaDiff Voice Processor → Audio Settings…* (the iPlug2 preferences dialog). No audio
device is needed for file processing.

## What the window contains

- **Header**: preset drop-down (factory + user presets), New / Load… / Save / Save As… / Delete,
  state text (`factory`, `user`, `unsaved`, `· modified` when the live settings differ from the
  loaded/saved file), full-chain Bypass, current latency.
- **One panel per module in processing order**: Input, Equalizer, Expander, De-esser,
  Compressor 1, Compressor 2, Enhancer, Output. The panels are generated from
  `core::paramTable()`, so **every parameter in the registry has a control** and new parameters
  appear automatically: knobs with exact value read-outs (double-click to type), toggles for
  booleans, tab switches / menus for enums, tooltips with the portable key and the range.
  The Equalizer shows its five bands as rows (On, Type, Frequency, Gain, Q) under a live
  response curve. The Expander shows the threshold actually in use and why.
- **Meters**: input and output sample peaks (L/R, with peak hold) and gain reduction for the
  expander and both compressors, read from `Engine::meters()` every 20 ms.
- **File bar**: input WAV, output WAV (default `<input>-processed.wav`), output encoding
  (keep / PCM 16 / 24 / 32 / float), Overwrite opt-in, **Analyze** (measure only) and **Process**
  (render through the current settings). Jobs run on a worker thread; the GUI never blocks. The
  result line shows duration, peak, speech level, noise floor, the resolved expander threshold
  and the output peak. Processing goes through the same transactional path as the CLI
  (temp file → verify → atomic replace; the input is never overwritten).

## Presets

- User presets live in `~/Library/Application Support/RaDiff Voice Processor/Presets`
  (`%APPDATA%\RaDiff Voice Processor\Presets` on Windows) and are plain `.radvoice.json` files,
  interchangeable with the CLI and the future plug-ins.
- Factory presets are read-only; *Save* on a factory preset behaves like *Save As…* into the user
  folder. *Delete* only ever deletes files in the user folder and asks first.
- The de-esser's *Listen* monitor is never written to a preset (see `core::isPersistent`).
- Session state (`PLUG_DOES_STATE_CHUNKS`) stores values keyed by their portable path
  (`expander.thresholdDbfs`, …) plus the preset metadata, so a future parameter reorder cannot
  corrupt a saved session.

## Live input vs. file mode

The speech-relative expander threshold needs a measurement of the material. File jobs get it from
the analysis pass. Live input uses `Engine::setSpeechTracking(true, 20 s)`: the gated speech
level is tracked online and the threshold follows it, refreshed every 100 ms, once about one
second of speech has been heard; until then the absolute threshold applies and the Expander
panel's "Threshold in use" line says so. Measured on the reference take it was within 1 dB of
the file analysis after 12 s (docs/TEST_RESULTS.md). The Meters panel shows IN/OUT peaks and the
gain reduction of the expander (EXP), de-esser (DS) and both compressors. Preview playback (next
section) is "live input from a file": the same tracker, meters and bypass apply.

## Preview playback (hear a file through the live settings)

The **PREVIEW** strip above the file bar plays a WAV through the live engine: *Load…* (choosing an
Input file in the file bar also loads it here), *Play/Pause*, *Stop*, *LOOP*, a position bar
(click or drag to seek), time read-out, file name / rate / channels, and a **SOURCE** indicator
that switches from `LIVE INPUT` to `FILE PREVIEW` while playing. While the file plays it replaces
the audio-device input inside `ProcessBlock` (the microphone is muted meanwhile) and everything
else is untouched: every knob acts immediately, the meters show the file's levels and the stages'
gain reduction, the header PROCESS/BYPASSED toggle is the A/B, the speech tracker follows the
file, and the output goes to the selected output device. Stop or the end of the file (unless
looping) returns to the live input.

Implementation: `radiff::ui::FilePlayer` (`src/ui`, toolkit-free, tested in
`tests/ui/test_file_player.cpp`). The file is read on the UI thread into one of two slots and
published to the audio thread without locks or allocations (atomic active/reading slot indices;
the UI never overwrites a slot a render is reading). When the file's sample rate equals the
device rate the samples are copied bit-exactly; otherwise a 32-tap windowed-sinc interpolator
(Blackman window, cut-off at 95 % of the lower Nyquist, kernel normalised per output sample)
resamples on the fly: 48 kHz → 44.1 kHz keeps 1 kHz and 6 kHz tones within 0.2 dB (tested).
A/B at *matched loudness* is not implemented: bypass compares at the natural level difference.

## Automation hooks (debug/documentation)

- `RADIFF_SCREENSHOT=<file.png>`: renders the UI once into an offscreen layer, writes the PNG and
  exits. Works without macOS screen-recording permission and for the Metal backend.
- `RADIFF_AUTO_PROCESS_IN=<in.wav>` (+ optional `RADIFF_AUTO_PROCESS_OUT=<out.wav>`): runs one
  Process job through the GUI code path (overwrite on), prints the result to stderr and exits
  (or takes the screenshot first when both are set).
- `RADIFF_AUTO_PLAY=<wav>`: loads the file into the preview player, seeks to 10 s, plays for
  about 3 s through the live engine and the real audio device, prints one line with the engine
  meters (current and maximum input/output peaks, per-stage gain reduction) and the player
  position to stdout, then exits (after the screenshot when `RADIFF_SCREENSHOT` is also set).

Verified on 2026-10-21 (macOS 27, Apple M1 Max, Debug, NanoVG/Metal): the app builds, opens,
renders every panel, and the auto-processed `A-raw-TOM0010679.wav` is **bit-identical** to the
CLI render with the same preset (`radiff_testtool compare-wav … --tol 1e-6` → max abs diff 0).
Preview verified the same day with `RADIFF_AUTO_PLAY`: the 48 kHz file played through the
44.1 kHz device stream (resampler path), position advanced 10.0 → 13.0 s in 3 s, input peaks up to
−7.6 dBFS and output peaks up to −10.9 dBFS reached the meters, the expander showed 11 dB of
reduction in a pause and the live tracker resolved the threshold to −33.7 dBFS. Not exercised by
a person yet: clicking Play/Stop/Loop and dragging the position bar (code paths written; the
player logic behind them is unit-tested).

## Windows

`RaDiffVoice.exe` compiles and links on GitHub Actions (MSVC 2022, NanoVG/GL2) since 2026-10-21 and
`test_ui` passes there, so `main.rc`, the icon and the preset/font copy step are at least accepted
by the toolchain. **Nobody has launched the Windows app yet**: window creation, GL2 rendering,
resource lookup next to the exe and audio-device selection are unverified until someone runs the
CI artifact on a Windows PC.

## Not in this phase (spec §7 items still open)

- Batch processing of several files, collision policies, per-file log.
- Preview A/B at *matched* loudness and a waveform display (preview playback itself exists).
- Drag & drop of files onto the window.
- Keyboard navigation between controls beyond what IGraphics provides.
- Per-stage transfer-curve displays for the dynamics modules.

## Notes for maintainers

- iPlug2's CMake (`iPlug2.cmake`, `find_package(iPlug2)`) is included only in
  `src/standalone/CMakeLists.txt`; it sets directory-scoped globals.
- Our GUI glue (`RaDiffVoiceApp.*`, `RaDiffControls.h`) compiles with warnings but not `-Werror`
  (iPlug2 headers warn). Everything toolkit-independent is in `radiff::ui` (`src/ui`) under the
  strict flags: `PresetLibrary` (scan/load/save/delete/dirty), `JobRunner` (worker thread around
  `app::processFile`), `ParamText` (labels, ranges, tooltips).
- The bundle identifier is derived by iPlug2 as `com.radiff.app.RaDiffVoice`
  (`BUNDLE_DOMAIN.BUNDLE_MFR.app.BUNDLE_NAME`); `Info.plist` must match or resources are not found.
- Icons: `src/standalone/resources/icon/make_icons.py` regenerates `RaDiffVoice.icns/.ico`
  (original design, no image library needed).

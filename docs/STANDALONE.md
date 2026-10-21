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
the analysis pass. For live input the engine does not have an online tracker yet, so
`speechRelative` presets fall back to the absolute threshold there; the Expander panel says so.
(The hook for the tracker is marked `MAIN-MERGE` in `RaDiffVoiceApp::OnReset`.)

## Automation hooks (debug/documentation)

- `RADIFF_SCREENSHOT=<file.png>`: renders the UI once into an offscreen layer, writes the PNG and
  exits. Works without macOS screen-recording permission and for the Metal backend.
- `RADIFF_AUTO_PROCESS_IN=<in.wav>` (+ optional `RADIFF_AUTO_PROCESS_OUT=<out.wav>`): runs one
  Process job through the GUI code path (overwrite on), prints the result to stderr and exits
  (or takes the screenshot first when both are set).

Verified on 2026-10-21 (macOS 27, Apple M1 Max, Debug, NanoVG/Metal): the app builds, opens,
renders every panel, and the auto-processed `A-raw-TOM0010679.wav` is **bit-identical** to the
CLI render with the same preset (`radiff_testtool compare-wav … --tol 1e-6` → max abs diff 0).

## Windows

`src/standalone/resources/main.rc`, `RaDiffVoice.ico` and the CMake copy step for presets/fonts are
**authored but not executed** (no Windows machine, no CI minutes). First run on Windows should
check: resource compilation, font lookup next to the exe, `bundledResourceDirectory()`.

## Not in this phase (spec §7 items still open)

- Batch processing of several files, collision policies, per-file log.
- Preview playback with A/B at matched loudness and a waveform display.
- Drag & drop of files onto the window.
- Keyboard navigation between controls beyond what IGraphics provides.
- Per-stage transfer-curve displays for the dynamics modules.
- A fourth gain-reduction meter for the de-esser (`MAIN-MERGE` marker in `OnIdle`).

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

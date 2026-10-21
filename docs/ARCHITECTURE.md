# Architecture

One source tree, one DSP engine, every artifact. The build is layered so that platform
code cannot leak into audio processing.

```
            ┌──────────────┐ ┌──────────────┐ ┌──────────────┐ ┌──────────────┐
 artifacts  │ RaDiffVoice  │ │ Standalone   │ │ VST3 wrapper │ │ AUv2 wrapper │
            │ CLI          │ │ app (iPlug2) │ │ (iPlug2)     │ │ (iPlug2)     │
            └──────┬───────┘ └──────┬───────┘ └──────┬───────┘ └──────┬───────┘
                   │                └────────┬───────┴────────────────┘
                   │                  ┌──────┴───────┐
                   │                  │ radiff::ui   │  platform-independent controls (Phase 5)
                   │                  └──────┬───────┘
            ┌──────┴─────────────────────────┴──────┐
            │ radiff::app   jobs, batch, transactional output, exit codes │
            └──────┬──────────────────┬─────────────┘
       ┌───────────┴───┐      ┌───────┴────────┐
       │ radiff::audioio│      │ radiff::presets│   WAV I/O · .radvoice.json
       └───────┬───────┘      └───────┬────────┘
               └──────────┬───────────┘
                   ┌──────┴───────┐      ┌───────────────────┐
                   │ radiff::core │      │ radiff::platform  │  the ONLY OS-specific code
                   │  DSP engine  │      │  posix/ macos/ windows/ │
                   └──────────────┘      └───────────────────┘
```

| Target | Directory | May depend on | Must not contain |
|---|---|---|---|
| `radiff::core` | `src/core` | C++ standard library only | file I/O, threads, `<filesystem>`, any `#ifdef` for an OS, allocation in `process()` |
| `radiff::platform` | `src/platform` | OS SDKs | DSP, UI |
| `radiff::audioio` | `src/audioio` | core, platform | OS APIs |
| `radiff::presets` | `src/presets` | core, platform, nlohmann/json | paths, binary blobs |
| `radiff::app` | `src/app` | everything above | UI toolkit, plug-in SDKs |
| `src/cli` | | app | display, audio devices |
| `src/ui`, `src/standalone`, `src/plugins/*` | | app, iPlug2, VST3 SDK | DSP |

## Rules that keep it portable

- **Paths** are `std::filesystem::path` internally and UTF-8 `std::string` at the edges
  (CLI arguments, JSON, logs). Conversions go through `radiff::platform::pathFromUtf8/pathToUtf8`.
  Never `std::string` → `path` directly: on Windows that is a lossy ANSI conversion.
- **Command-line arguments** are obtained via `platform::commandLineUtf8()`, which re-reads
  the wide command line on Windows so non-ASCII paths survive.
- **Atomic output** uses `platform::atomicReplace()` (`rename(2)` + directory fsync on
  POSIX; `ReplaceFileW`/`MoveFileExW` with write-through on Windows). Higher layers only
  see `app::writeFileTransactionally()`.
- **Numbers in text** are parsed/formatted with the classic locale
  (`platform::parseNumber`) so a German Windows and a US Mac agree on `1.5`.
- **No** drive-letter, separator, case-sensitivity, working-directory or home-directory
  assumptions. Tests use a build-tree temp dir and exercise a Unicode file name.
- **Line endings**: `.gitattributes` normalises to LF; Windows scripts are CRLF.
- **Floating point**: `-fno-fast-math` / `/fp:precise`. Parity between platforms and
  architectures is tested within a documented tolerance, not for bit-identity.

## Threading contract of the engine

`Engine::prepare/release/reset` – non-realtime thread.
`Engine::setParameters/process` – audio thread; no allocation, locks, exceptions or I/O.
Hosts hand the engine a `ParameterSet` snapshot (trivially copyable) rather than individual
parameter writes, so plug-in automation and preset loads are applied atomically.

## Parameter table

`src/core/include/radiff/core/Parameters.h` is the single source of truth. Each entry
carries the stable preset key, display name, unit, range, default and step. The preset
format, CLI `--set`, plug-in parameter lists and UI are all generated from this table;
adding a parameter is one line plus its DSP.

**The parameter set currently holds a Phase 1 placeholder chain (bypass, input gain,
output gain).** The full processing chain is specified by the master development
specification and is implemented in Phase 2.

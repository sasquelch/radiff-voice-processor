# `.radvoice.json` preset format

Presets are UTF-8 JSON text files with LF line endings and a `.radvoice.json` extension.
The same file is read by the macOS and Windows standalone apps, both CLIs, the VST3
plug-ins and the AU plug-in.

```json
{
  "format": "radvoice",
  "format_version": 1,
  "name": "Morning Show",
  "description": "Bright, tight",
  "author": "Jöe",
  "parameters": {
    "bypass": 0.0,
    "input_gain_db": 2.5,
    "output_gain_db": -4.0
  }
}
```

Rules:

- `format` must be `"radvoice"`; `format_version` is an integer, currently `1`.
- `parameters` maps stable keys (from `radiff::core::paramTable()`) to numbers (booleans
  accepted for switch parameters). Keys are documented by `RaDiffVoiceCLI --list-params`.
- Missing parameters take their defaults; unknown parameters are ignored with a warning
  (forward compatibility); out-of-range values are clamped. This means a preset saved by a
  newer build loads in an older one.
- The file **never** contains file paths, machine names, sample rates, channel layouts,
  binary blobs or OS-specific data. Input/output paths are operational settings that live
  in the CLI arguments, the app's session, or the PlayoutONE wrapper configuration.
- Serialisation is canonical (fixed key order, 2-space indent, trailing newline) so the
  same preset produces a byte-identical file on every platform; this is tested.
- Plug-in session state embeds the full parameter set in the host's project; it never
  references the preset file, so moving a project between machines does not break it.

The current parameter set is the Phase 1 placeholder; the full set follows the master
specification in Phase 2 and will keep `format_version` at 1 because it is purely additive.

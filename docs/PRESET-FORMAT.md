# `.radvoice.json` preset format

Presets are UTF-8 JSON text files with LF line endings and a `.radvoice.json` extension.
The same file is read by the macOS and Windows standalone apps, both CLIs, the VST3
plug-ins and the AU plug-in. Schema version **1**.

```json
{
  "schemaVersion": 1,
  "engineVersion": "0.1.0",
  "algorithmVersion": 1,
  "name": "Broadcast Voice - Balanced",
  "description": "...",
  "author": "RaDiff",
  "signalOrder": ["input", "eq", "expander", "deesser", "compressor1", "compressor2", "presence", "output"],
  "bypass": false,
  "modules": {
    "input": {"gainDb": 0.0, "dcBlockEnabled": true, "highPassEnabled": true, "highPassHz": 80.0, "highPassSlopeDbOct": 12.0},
    "eq": {"enabled": true, "bands": [
      {"enabled": false, "type": "lowShelf", "frequencyHz": 120.0, "gainDb": 0.0, "q": 0.7},
      {"enabled": false, "type": "bell", "frequencyHz": 300.0, "gainDb": 0.0, "q": 1.0},
      {"enabled": false, "type": "bell", "frequencyHz": 1200.0, "gainDb": 0.0, "q": 1.0},
      {"enabled": false, "type": "bell", "frequencyHz": 3500.0, "gainDb": 0.0, "q": 1.0},
      {"enabled": false, "type": "highShelf", "frequencyHz": 10000.0, "gainDb": 0.0, "q": 0.7}
    ]},
    "expander": {"enabled": true, "thresholdDbfs": -48.0, "ratio": 3.0, "kneeDb": 6.0, "attackMs": 5.0, "holdMs": 40.0,
                 "releaseMs": 60.0, "hysteresisDb": 3.0, "detector": "rms", "rmsWindowMs": 10.0,
                 "sidechainHighPassEnabled": false, "sidechainHighPassHz": 100.0,
                 "sidechainLowPassEnabled": false, "sidechainLowPassHz": 8000.0,
                 "lookaheadMs": 15.0, "maxAttenuationDb": 20.0, "wet": 1.0, "stereoLink": true,
                 "thresholdMode": "speechRelative", "relativeThresholdDb": -12.0, "transientRejectMs": 12.0},
    "deesser": {"enabled": false, "frequencyHz": 6000.0, "bandwidthOctaves": 1.0, "thresholdDbfs": -30.0,
                "maxReductionDb": 6.0, "attackMs": 1.0, "releaseMs": 60.0, "listen": false, "stereoLink": true},
    "compressor1": {"enabled": true, "thresholdDbfs": -24.0, "ratio": 2.5, "kneeDb": 6.0, "attackMs": 10.0, "releaseMs": 150.0,
                    "makeupGainDb": 0.0, "detector": "rms", "rmsWindowMs": 10.0, "sidechainHighPassEnabled": false,
                    "sidechainHighPassHz": 100.0, "autoRelease": false, "wet": 1.0, "stereoLink": true, "autoMakeup": false},
    "compressor2": {"enabled": false, "...": "same keys as compressor1"},
    "presence": {"enabled": false, "lfDetail": 0.0, "lfFrequencyHz": 100.0, "hfDetail": 0.0, "hfFrequencyHz": 3500.0,
                 "hfDynamic": 0.5, "makeupGainDb": 0.0}
  },
  "output": {"gainDb": 0.0}
}
```

## Rules

- `schemaVersion` (integer) is required; `1` is the only version today. A newer file is
  rejected with a clear error; older versions will be migrated in `fromJson()` when the schema grows.
- `algorithmVersion` records which DSP algorithm produced/was tuned with the preset
  (`core::kAlgorithmVersion`). `engineVersion` is informational.
- `signalOrder`, if present, must list implemented modules in the fixed v1 order. Modules it
  omits are fine (files written before the de-esser/presence stages existed list six names and
  still load); unknown future module names (`multibandExpander`, `saturation`, `limiter`) are
  accepted and ignored with a warning, so files from later versions still load.
- `deesser.listen` (detection monitor) is operator-only state: it is always written as `false`
  and ignored when read, so a preset can never put a monitor mode on air.
- Every parameter key is the one listed by `RaDiffVoiceCLI --list-params`
  (`module.key`, or `eq.bandN.key`). Missing parameters take defaults; unknown parameters or
  modules are ignored with a warning; out-of-range values clamp; booleans may be written as
  `true/false` or `0/1`; enums (`type`, `detector`) are strings, numbers are also accepted.
- `expander.thresholdMode` is `"absolute"` (use `thresholdDbfs`) or `"speechRelative"` (threshold =
  measured speech level of the recording + `relativeThresholdDb`; the measurement itself is
  made per file by the host and is **not** stored in the preset). Files written before these
  keys existed load unchanged: the defaults are `absolute`, `-15`, `0`.
- The file **never** contains file paths, machine names, sample rates, channel layouts, binary
  blobs or OS-specific data. Operational settings live in CLI arguments, the app session or the
  PlayoutONE wrapper configuration.
- Serialisation is canonical (fixed key order, 2-space indent, values rounded to 1e-6, trailing
  newline) so the same preset produces byte-identical files on every platform (tested; the
  factory presets are regenerated through the serializer).
- Plug-in session state embeds the full parameter set; it never references the preset file.

Factory presets live in `presets/factory/`. They are original starting points and carry a
description saying that they are not yet approved by listening tests.

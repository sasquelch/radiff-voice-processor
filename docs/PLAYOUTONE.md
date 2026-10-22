# PlayoutONE / WebVT integration (Phase 7)

`scripts/playoutone/WebVT-RaDiff.bat` replaces the REAPER + RX render step of the station's
WebVT post-processing script with `RaDiffVoiceCLI.exe`. It is a drop-in: PlayoutONE calls it
with the same arguments as before (`%1` input WAV, `%2` final output, `%4` Stereo Tool `.sts`
preset, `%6` Stereo Tool license key).

Input may be WAV or MP3 (WebVT passes either); RaDiff decodes MP3 itself and always hands
Stereo Tool a WAV. Chain: input → **RaDiff** (expander with click ducking, de-esser, EQ, compressors, enhancer
as set in the preset) → **Stereo Tool** (`stereo_tool_cmd.exe`, station sound) → final file.
RaDiff runs first on purpose: the expander and de-esser want the natural voice, and Stereo
Tool's AGC would otherwise raise breaths and room tone before the gate sees them. If no `.sts`
preset is passed or `stereo_tool_cmd.exe` is missing, RaDiff writes the final file directly.

Install on the automation machine (no sound card is ever opened):

1. Unzip the Windows release; copy `RaDiffVoiceCLI.exe` to `C:\PlayoutONE\Modules\RaDiff\`.
2. Save the preset you tuned in the app (Save As…) and copy the `.radvoice.json` file to
   `C:\PlayoutONE\Modules\RaDiff\Presets\` (or a factory preset from `Presets\factory`).
3. Edit the three paths at the top of the batch file, keep your existing WORKDIR/LOGDIR.
4. Run it by hand once: `WebVT-RaDiff.bat "D:\in.wav" "D:\out.wav" "" "" "" ""` (no Stereo
   Tool) and check the log in LOGDIR; then point PlayoutONE at it.

First production log (2026-10-21) showed RaDiff reading a take as **digital silence** (`in -144.0
dBFS`, `active 0.0 s`) while Stereo Tool then happily processed 7 MB of zeros. The take had been
deleted by PlayoutONE before it could be examined, so the batch file now defends itself:
`--wait-for-input 30` (RaDiff waits until the file exists and has stopped growing),
`--reject-silent-input` (an all-zero take exits 7 instead of producing silent audio), three
attempts 3 s apart, and on final failure a copy of the input is kept as
`WORKDIR\failed_<job>_input.wav` for diagnosis. `--verbose` also prints the WAV layout (format
tag, bits, chunk list, data offset/size, file size, "SHORT by N" if truncated).

`KEEP_INPUTS=1` at the top of the batch file keeps a copy of every input and final output in
WORKDIR (`kept_<job>_input.wav` / `kept_<job>_final.wav`) for tuning on real takes; set it back
to 0 afterwards, nothing deletes them.

Logging: RaDiff's `--verbose` output (latency, levels, timing, resolved expander threshold,
input layout) goes to the per-job log; the license key is never echoed. Exit codes from RaDiff are mapped to
plain-text reasons in the log; the batch file exits 1 on any failure and never leaves a partial
output file (RaDiff publishes atomically; a failed Stereo Tool step leaves no final file).

Besides the per-job log, every run appends one timestamped record to `LOGDIR\radiff.log`
(`--log`): paths, preset and overrides, input layout, levels, resolved threshold, result. Not yet
done from the spec's Phase 7 list: a PlayoutONE-side settings file, and a verified good run on the
real automation machine (the first run processed a take that decoded as silence).

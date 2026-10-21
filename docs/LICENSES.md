# Third-party licence review

Goal: closed-source commercial distribution on macOS and Windows with no paid frameworks,
no per-seat/runtime royalties and no copyleft obligations on our code.

| Component | Version | Licence | Use | Verdict |
|---|---|---|---|---|
| Steinberg VST 3 SDK (`base`, `pluginterfaces`, `public.sdk`, `cmake`) | 3.8.1 (build 84) | **MIT** (relicensed by Steinberg with VST 3.8, Oct 2025; `LICENSE.txt` in the submodule) | VST3 plug-in format, validator | OK. Keep the copyright/licence text in the distribution. `vstgui4` is not used. The "VST" word mark remains a Steinberg trademark; usage follows Steinberg's trademark guidelines (e.g. "VST is a trademark of Steinberg Media Technologies GmbH" in about box/docs). |
| iPlug2 | commit `39ea73aa` (master, Oct 2026) | zlib-style (`LICENSE.txt`) | standalone app shell, plug-in wrappers, UI toolkit | OK for closed source. iPlug2 bundles third-party code (WDL, NanoVG, etc.) under permissive licences; each is listed in its `LICENSE.txt` and will be re-audited when the UI layer is wired in (Phase 5). |
| nlohmann/json | 3.12.0 | MIT | preset files | OK |
| doctest | 2.5.3 | MIT | tests only, not shipped | OK |
| Apple frameworks (Foundation, AudioUnit, CoreAudio…) | system | Apple SDK licence | macOS builds only | OK; not required on Windows |
| Windows SDK / MSVC runtime | VS 2022 | Microsoft | Windows builds only; static CRT (`/MT`) so no redistributable is needed | OK |
| Fonts, icons | — | — | none yet | Any added asset must be OFL/CC0/MIT and listed here |

Not used, by policy: JUCE (GPL/commercial), AAX SDK (requires Avid agreement), VST2 SDK
(no longer licensable), commercial DSP libraries, commercial installers.

Attribution text for the About box / README of release packages:

> VST is a trademark of Steinberg Media Technologies GmbH. This software uses the VST 3 SDK
> (MIT licence, © Steinberg Media Technologies GmbH), iPlug2 (zlib licence) and
> nlohmann/json (MIT licence).

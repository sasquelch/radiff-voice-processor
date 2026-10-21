# Release checklist

Nothing ships until every row is either checked with evidence in `docs/TEST_RESULTS.md` or
explicitly listed under "Known limitations" in the release notes.

## Build
- [ ] Clean clone (`git clone --recursive`) builds with `scripts/build-mac.sh universal` on macOS.
- [ ] Clean clone builds with `scripts\build-windows.ps1` on Windows 10/11 x64 (MSVC 2022).
- [ ] `lipo -info` shows arm64 + x86_64 for every macOS binary/bundle; `minos 11.0`.
- [ ] Version bumped in `CMakeLists.txt`; `core::kAlgorithmVersion` bumped if the sound changed.

## Automated tests
- [ ] `ctest` green on macOS arm64 (Debug, ASAN, Universal Release).
- [ ] `ctest` green on Windows x64 (Release).
- [ ] x86_64 test binaries executed (Rosetta or Intel Mac) or documented as not executed.

## Plug-ins (Phase 5+)
- [ ] VST3 validator passes on macOS and Windows.
- [ ] `auval -v aufx <subtype> <manu>` passes.
- [ ] Hands-on: REAPER (mac/win), Audition (mac/win), Logic Pro (AU): load, automate, save/reopen,
      offline render equals real-time render within tolerance. Record versions used.

## Presets and CLI
- [ ] Every factory preset loads with zero warnings on every edition.
- [ ] Cross-edition parity: same WAV + preset through mac CLI, win CLI, standalone, VST3, AU
      within documented tolerance.
- [ ] Exit codes verified for each failure class; no partial output ever published.

## Listening
- [ ] Scorecards completed for the material in section 12 of the spec (soft words, consonants,
      sentence endings, breaths, clicks, noise, loud delivery, plosives, sibilance, pauses).
- [ ] A/B at matched loudness: raw vs RaDiff vs RaDiff → Stereo Tool.
- [ ] No factory preset enables an unproven destructive stage.

## Packaging and licences
- [ ] `THIRD_PARTY_LICENSES.md` matches the pinned submodule commits; notices bundled.
- [ ] macOS: unsigned zip for internal use; signed + notarized build if Developer ID is available
      (documented Gatekeeper behaviour otherwise).
- [ ] Windows: portable zip; SmartScreen note in README.
- [ ] Factory presets and install/rollback instructions included.
- [ ] `docs/DEVELOPMENT_STATUS.md` matrix updated with the exact state of this release.

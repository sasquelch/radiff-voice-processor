# Pinned dependencies

| Submodule | Upstream | Pinned | Licence |
|---|---|---|---|
| `nlohmann_json` | https://github.com/nlohmann/json | `v3.12.0` | MIT |
| `doctest` | https://github.com/doctest/doctest | `v2.5.3` | MIT |
| `vst3sdk` | https://github.com/steinbergmedia/vst3sdk | `v3.8.1_build_84` (nested: `base`, `pluginterfaces`, `public.sdk`, `cmake` only) | MIT |
| `iPlug2` | https://github.com/iPlug2/iPlug2 | `39ea73aafe6f62fa0b52b9c633d2a480f5abb353` | zlib |
| `minimp3` | https://github.com/lieff/minimp3 | `ea99364` (master, 2026) | CC0 1.0 (public domain) |

Update procedure: `cd third_party/<name> && git fetch && git checkout <tag>`, rebuild and
run all tests on macOS **and** Windows CI, update this table and `docs/LICENSES.md`, commit
the submodule pointer in the same change.

`vstgui4`, `doc` and `tutorials` inside `vst3sdk` are intentionally not initialised.

# Codex project instructions — OSTF Studio

## Mission
Build a cross-platform, AI-native OSTF-based DAW. OSTF 5.0 is the canonical musical score; OSTFP is the standard project container. This repository is **a UI bootstrap only**, not a completed DAW.

## Do not fabricate OSTF standard details
- Before implementing OSTF/OSTFP serialization, obtain the official user's final OSTF 5.0 release package (schemas, spec, sample files) from the user or an authorized repository.
- The official spec governs exact key names, normative package structure, hash links, vocal, production and distributed profiles. No guessed fields.
- .ostf score must remain portable and DAW-independent. Hardware IDs/credentials stay in local settings, not the score.
- OSTFP should preserve supported session metadata and assets without embedding third-party plugins or proprietary binaries.

## Build + tooling
- C++20, JUCE 9.0.5, CMake 3.22+; Microsoft Windows primary, macOS CI preview.
- Keep core/model code independent of OS and JUCE wherever practical.
- `cmake -S . -B build-core -DOSTF_STUDIO_BUILD_GUI=OFF && cmake --build build-core && ctest --test-dir build-core --output-on-failure` checks the core without downloading JUCE.
- `cmake -S . -B build -DOSTF_STUDIO_BUILD_GUI=ON && cmake --build build --config Release` fetches JUCE and builds the GUI.
- GitHub Actions builds Windows/macOS, uploads unsigned preview ZIPs; it is **not** evidence of real-device validation.
- Start changes in feature branches; keep PR review before merging main.

## Architecture expectations
1. Global Musical Timeline: sections/chords/key/meter/tempo/phrasing etc. over aligned track lanes.
2. Separated source musical time, finite performance plan time, session/audio clock.
3. OSTF TEXT import/export, clipboard, range export and merge, with explicit approval for destructive edits.
4. SMF Type 1 export, SVP conversion with versioned mapping, VST3 hosting and future ARA; don't claim automatic SVP injection works without documented API/proof.
5. Multi-device audio engine: ASIO/WASAPI/CoreAudio separated; plan for independent clocks, buffering/resampling/latency compensation.
6. AI commands as validated proposal/diff applied at safe musical boundaries, not in audio callback.
7. Remote sessions exchange edit/intention/transport semantics; do not claim zero-latency WAN live performance.
8. Audio files optional in OSTFP; music score stays independent.

## Development discipline
- Label mock controls/data as demo; avoid fake functionality.
- No secret API keys in git; user must explicitly configure OAuth/API credentials.
- Respect JUCE/Tracktion/Steinberg/ARA/licensing and plugin vendor terms; review before public release.
- Add a regression test for each new implemented domain rule.
- Record code vs compile vs real-device validation separately.
- Do not modify canonical OSTF spec from inside the DAW without explicit authorization.

## First Codex task
1. Inspect repository and confirm core tests build.
2. Build GUI on Windows/macos GitHub Actions; fix **actual** compile errors found.
3. Produce a phase plan for OSTF 5.0 ingestion once official spec files are supplied.
4. Do not invent official OSTF sample files or claim DAW features are implemented yet.

# OSTF Studio — Codex bootstrap (v0.1.0)

**Status: starter repository / UI demonstration, not a functional DAW.**

OSTF Studio is intended to become an AI-native DAW with OSTF 5.0 as canonical musical score, OSTFP as the project format, a powerful global musical timeline, VST3/AU hosting, optional audio assets, AI arrangement, and future distributed sessions.

## This starter contains
- Portable C++20 timeline model with unit tests (internal only; not OSTF spec definitions)
- JUCE 9.0.5 desktop window with a **32-bar demo** ruler, global musical lanes, and named arrangement regions aligned to the same bar positions
- `.ostf` and `.json` **basic JSON object/array inspection** using JUCE, without OSTF schema validation
- Read-only **OSTFP 1.0 preview** for a bounded subset of score-only OSTF 5.0.0 packages. **Open demo** loads the bundled official 16-bar song. **Open .ostfp / .ostf** can open the same package from disk.
- Select an arrangement row to view its pitched notes in the read-only piano roll. The ruler shows song bar numbers and the keyboard follows the track's pitch range. Sound-slot percussion is outside the pitched view.
- The official demo opens on startup with a pitched track selected. The transport plays pitched notes through a built-in preview instrument on the selected audio output. Volume, solo and mute affect that preview. Enable a MIDI input and use **Record MIDI** to capture notes into the selected track for the current session. Recording starts at the beginning and retains captured notes in memory on Stop. Audio recording, OSTFP save and plugin signal routing are pending.
- Use **File** to open a project or the demo, **View** to switch editors, and **Settings → Audio / MIDI Devices** to select devices. On Windows, choose **ASIO** to use an installed manufacturer driver. `OSTF_STUDIO_ENABLE_ASIO` defaults to `ON` and affects Windows builds only.
- Arrangement rows share volume, mute and solo with the Mixer. Use a track's **Plug-in** slot to select a local VST3, then choose **Load VST3** to open its parameter editor. The slot holds one instance for the current session. Plug-in audio routing and state persistence remain pending. On macOS, AU files can be assigned for the session.
- The startup screen shows workspace preparation. **Plug-ins** opens a manufacturer-grouped VST3 browser. The first opening without a cache starts the scanner in a separate process and shows the current plug-in and scan progress. Subsequent openings use the saved local XML catalogue. Drag a plug-in onto an arrangement row to instantiate it without a new directory scan. **Scan VST3** checks for additions or changed files. Track slots display when an individual plug-in is loading.
- The Mixer puts voicing-role, vocal-role and pitched tracks on the left. Source chord voicings are shown as Global Timeline context when present. Use **Out** for Master or Bus A/B, **Send** for per-return send levels, and **+ Return** to add up to eight returns. Buses and post-fader returns sum to Master. The pre-fader **C** switch sends to Cue, using the third and fourth enabled output channels. Cue remains silent when fewer than four outputs are active. These paths apply to the built-in preview instrument and carry no insert or send DSP yet.
- GitHub Actions Windows + macOS **preview** builds, unsigned
- `AGENTS.md` instructions for continuing in Codex
- Architecture/roadmap/checklists

**Not included:** bundled third-party plugins, plug-in audio processing, full OSTFP conformance or writing, SMF/SVP exporters, network sync, AI connection or signed binaries.

## Resume development from GitHub

```bash
git clone https://github.com/yoh-nak/OSTF-Studio.git
cd OSTF-Studio
git branch -avv
```

Open the clone in Codex. Read the [development rules](AGENTS.md), the [current handoff](HANDOFF.md), and the [architecture and roadmap](docs/ARCHITECTURE.md). For an existing clone, fetch `origin` and check the working tree before switching to the relevant remote branch. Install the local build prerequisites below on each machine. Keep GitHub credentials, personal settings, and generated build files local.

## Quick start — Codex desktop
1. Extract this ZIP into a folder named `OSTF-Studio` (retain all directories).
2. In Codex desktop, select **Add project / Open folder**, then point it to the extracted folder.
3. Read `AGENTS.md`, then use `CODEX_START_HERE.md` as the first prompt.
4. Optional: create a GitHub repository `OSTF-Studio` and push the files; link it in Codex Cloud so tasks persist in Git.

## Quick start — Codex Cloud
1. Create a new GitHub repo, add all contents of this ZIP to its root and commit.
2. At https://chatgpt.com/codex create/publish a cloud environment with this repo (if your plan permits).
3. Select the environment; paste the contents of `CODEX_START_HERE.md`.

## Build core tests (no JUCE download needed)
```bash
cmake -S . -B build-core -DCMAKE_BUILD_TYPE=Release -DOSTF_STUDIO_BUILD_GUI=OFF
cmake --build build-core --config Release
ctest --test-dir build-core -C Release --output-on-failure --no-tests=error
```

## Build GUI (fetches JUCE 9.0.5, needs network)
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DOSTF_STUDIO_BUILD_GUI=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure --no-tests=error
```

Install CMake 3.22 or later and Git for the JUCE download. On Windows use MSVC with the Windows SDK. On macOS use the Xcode Command Line Tools. GitHub Actions provides both build environments.

## Build outputs
The [build workflow](.github/workflows/build.yml) runs standalone core tests and builds the JUCE GUI on Windows x64 (Visual Studio 2022) and macOS arm64. Successful jobs upload a Windows installer and Portable ZIP, and a macOS DMG, under **Actions > run > Artifacts** for seven days. Publishing a stable release or pre-release requires an explicit workflow dispatch on `main`. See [Packaging and releases](docs/RELEASING.md) for version tags, installation behavior and macOS signing requirements. GUI interaction and audio-device behavior require separate real-device validation.

See the [development roadmap](docs/ARCHITECTURE.md#user-visible-milestones) for the official OSTF 5.0 intake requirements and the sequence for implementing DAW features.

## Important: OSTF 5.0 spec
The preview is based on the [official OSTF 5.0.0 source release at commit `d8ce48c`](https://github.com/yoh-nak/Open-Song-Timeline-Format-5.0/tree/d8ce48ca6c873a5a24a94bcc80d847bf33135b46). The bundled CC0 example and its SHA-256 are documented in [fixture provenance](tests/fixtures/README.md). The preview checks the package inventory and score hash, then displays supported source lanes. It currently requires a single meter, bar-aligned regions, and a score-only package. Full Schema validation, mixed meters, sub-bar events, profile documents, assets and playback remain future work.

## Licensing
JUCE is subject to its own licensing; do not assume donation-supported freeware is automatically exempt. Review the licensing and redistribution conditions for the final project before public release. No separate OSS licence is assigned to user-developed files in this starter.

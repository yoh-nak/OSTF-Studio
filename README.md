# OSTF Studio — Codex bootstrap (v0.1.0)

**Status: starter repository / UI demonstration, not a functional DAW.**

OSTF Studio is intended to become an AI-native DAW with OSTF 5.0 as canonical musical score, OSTFP as the project format, a powerful global musical timeline, VST3/AU hosting, optional audio assets, AI arrangement, and future distributed sessions.

## This starter contains
- Portable C++20 timeline model with unit tests (internal only; not OSTF spec definitions)
- JUCE 9.0.5 desktop window with **demonstration** global timeline and track arrangement lanes
- `.ostf` and `.json` **basic JSON object/array inspection** using JUCE, without OSTF schema validation
- GitHub Actions Windows + macOS **preview** builds, unsigned
- `AGENTS.md` instructions for continuing in Codex
- Architecture/roadmap/checklists

**Not included:** licensed third-party plugins, real VST3 hosting, sound engine, OSTFP parser/writer, SMF/SVP exporters, network sync, real AI connection, signed binaries, official OSTF 5.0 schema/spec/assets. These must be implemented and verified separately.

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
The [build workflow](.github/workflows/build.yml) runs standalone core tests and builds the JUCE GUI on Windows x64 (Visual Studio 2022) and macOS arm64. Successful jobs upload preview ZIPs under **Actions > run > Artifacts**. The macOS preview has no Developer ID signature or notarization. CI covers compilation, core tests, and packaging. GUI interaction and audio-device behavior require separate real-device validation.

See the [development roadmap](docs/ARCHITECTURE.md#user-visible-milestones) for the official OSTF 5.0 intake requirements and the sequence for implementing DAW features.

## Important: OSTF 5.0 spec
The canonical official OSTF 5.0 release archive was **not** attached to this starter. Import the actual final schema/spec/examples into `docs/vendor-spec/` only if redistribution is permitted, or provide Codex a path to them. Do not replace it with inferred fields from previous chat history.

## Licensing
JUCE is subject to its own licensing; do not assume donation-supported freeware is automatically exempt. Review the licensing and redistribution conditions for the final project before public release. No separate OSS licence is assigned to user-developed files in this starter.

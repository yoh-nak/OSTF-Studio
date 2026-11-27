# Codex project instructions — OSTF Studio

## Mission
Build a cross-platform, AI-native OSTF-based DAW. OSTF is the canonical musical score; OSTFP is the standard project container. This repository is **a UI bootstrap only**, not a completed DAW.

## Development and GitHub workflow
- Drive development through natural-language Codex requests. Automate source changes, tests, Git operations and GitHub Actions builds where practical.
- Work on feature branches and record changes in commits. Review the diff and verify tests before integrating into `main`. Follow the applicable permissions and approvals for pushes and integration. Release publication is a separate operation.
- Preserve source files and commit history. Check existing design and behavior before changing them, and limit changes to the requested scope.
- Maintain the feature inventory, roadmap and implementation status in `README.md` and `docs/ARCHITECTURE.md`. Distinguish implemented, in progress, and unimplemented features. Record CI compilation, tests, packaging and real-device app verification independently. Report unresolved failures and unverified behavior by platform.
- Read `HANDOFF.md` when resuming development. Update it in the same change as significant implementations, fixes or validation results. Keep branch context, platform verification and unresolved work reproducible from a fresh clone.

## Do not fabricate OSTF standard details
- For every format change, consult the current default branch, STATUS.json, registry and releases in the [official OSTF repository](https://github.com/yoh-nak/Open-Song-Timeline-Format). Check the released specification, schemas and examples for the particular file version. Record the reviewed revision and test fixtures when adding support. Keep historical readers and their pinned schemas reproducible. A draft or SDK milestone alone does not enable a new writer.
- The official spec governs exact key names, normative package structure, hash links, vocal, production and distributed profiles. No guessed fields.
- .ostf score must remain portable and DAW-independent. Hardware IDs/credentials stay in local settings, not the score.
- OSTFP should preserve supported session metadata and assets without embedding third-party plugins or proprietary binaries.
- Support standalone `.ostf` save/import/export alongside `.ostfp` projects. Prioritize OSTF TEXT copy, paste and partial import. Keep DAW production settings separate from the canonical score, and preserve OSTF portability and compatibility.

## Build + tooling
- C++20, JUCE 9.0.5, CMake 3.22+. Use shared source for Windows x64, macOS arm64 and macOS x86_64. Keep platform-specific code at its integration boundary.
- Keep core/model code independent of OS and JUCE wherever practical.
- Use the persistent Ninja tree by default. Run `python tools/build.py`, or pass `--target OSTFStudio` or a device target. Run `python tools/build.py --core --test` for the JUCE-free core. Use the same generator and toolchain in each tree.
- Follow [Incremental builds](docs/BUILDING.md) for parallel job budgets, compiler caches, release validation and isolated clean builds. Keep existing build directories during normal development. Keep version strings, device wrappers, DSP, validation and future SDK code in their own dependency scopes. Benchmark PCH changes before enabling them.
- Investigate and fix reproducible Windows or macOS build failures where possible. CI compilation, test results, and package generation are separate from real-device app behavior.

## Builds, distribution and updates
- Build Windows and macOS in GitHub Actions. Store development packages in Actions Artifacts for seven days. Artifact expiration applies only to those build outputs.
- Publish only explicitly authorized releases, using a unique Git tag for each version. Preserve all previous tags, Releases, assets and release history. Reject duplicate releases. Support stable releases and explicit pre-releases.
- Publish the current 1.0.0 public alpha series as numbered GitHub pre-releases, starting with `v1.0.0-public-alpha.1`. The experimental tags `v1.0.0-alpha.1` through `v1.0.0-alpha.3` identify earlier builds. Increment the public alpha number for the next authorized publication. Switch to a stable release only on explicit instruction.
- Advance the public alpha number only for an authorized publication. Keep the same number and revision when retrying an upload. Preserve prior downloads. Build and test each native Mac architecture and Windows from the same revision, verify versioned assets in a draft, and publish the alpha as a pre-release with Latest disabled.
- Refresh the bilingual illustrated guides, catalogue, CHANGELOG and limitations against the release candidate. Reuse current screenshots when the UI matches. Verify the public asset downloads after publication.
- License original Studio application and bundled VST3 code under GPL-3.0-only. Distribute JUCE under AGPLv3 and the Windows ASIO SDK under GPLv3, retaining third-party notices. Include the matching corresponding-source archive in each Release.
- Publish only after both platform builds and required tests succeed. Include Windows Setup.exe, Windows Portable ZIP, macOS DMG and release notes in the same Release. Track the product version, CI build number and publication date as described in `docs/RELEASING.md`.
- Detect existing installs and update safely. Preserve OSTF, OSTFP, audio assets and local settings. Uninstall only installer-owned files and shortcuts. Preserve external plugins and user data. The Portable ZIP runs without installing OSTF Studio.
- Document signing and validation for each platform. Track Windows Authenticode, macOS Developer ID and macOS notarization separately.

## Architecture expectations
1. Global Musical Timeline: sections/chords/key/meter/tempo/phrasing etc. over aligned track lanes.
2. Separated source musical time, finite performance plan time, session/audio clock.
3. OSTF TEXT import/export, clipboard, range export and merge, with explicit approval for destructive edits.
4. MIDI editing, SMF Type 1 export, Synthesizer V SVP conversion with versioned mapping, VST3/AU hosting and future ARA. Verify any automatic SVP integration against documented vendor APIs.
5. Multi-device audio engine: ASIO/WASAPI/CoreAudio separated; plan for independent clocks, buffering/resampling/latency compensation.
6. AI composition, arrangement and performance assistance use validated proposals and diffs applied at safe musical boundaries outside the audio callback.
7. Remote collaboration and playback sync exchange edit, intent and transport events and account for network latency.
8. Support audio import and playback. Audio files remain optional in OSTFP and independent of the portable score.

## Development discipline
- Label mock controls/data as demo; avoid fake functionality.
- Keep API keys, credentials and personal settings out of Git. Users explicitly configure OAuth/API credentials.
- Respect JUCE, Tracktion Engine, Steinberg, ARA and plugin vendor licensing. Distribute paid plugins, audio libraries and third-party content only with permission. The long-term distribution goal is free access with optional donations, subject to dependency licensing.
- Add automated coverage for new features where practical and a regression test for each new implemented domain rule.
- Do not modify canonical OSTF spec from inside the DAW without explicit authorization.

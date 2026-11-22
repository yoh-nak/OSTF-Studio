# OSTF Studio development handoff

Read this file after `AGENTS.md` when resuming work on another computer. Update it in the same change as significant implementations, fixes, or validation results. Record what changed, what was verified on each platform, and what remains to be done. Keep durable policy in `AGENTS.md` and detailed procedures in the linked documents.

## Current state

- OSTF Studio is a JUCE UI bootstrap. The Global Timeline displays a 32-bar internal demo with sections, chords, key/scale, meter, tempo, phrasing and named arrangement regions in five tracks. The ruler and both lane groups use the same tested, bar-based projection. The separate fixed-meter beat converter and the JSON object/array inspector remain available.
- The official OSTF 5.0 source release is pinned in [fixture provenance](tests/fixtures/README.md). The preview opens supported score-only `.ostfp` packages and maps their score and tracks into the renderer. The bundled example opens at startup and via **File → Open demo song**. Full schema conformance, writing, AI editing and remote collaboration are planned. See the [architecture and roadmap](docs/ARCHITECTURE.md).
- The OSTFP preview shows a piano roll when an arrangement row is selected. The official fixture tests verify pitch, note length and conversion from clip-relative to song ticks. Sound-slot percussion and direct note editing are pending.
- Startup selects the official demo's first pitched track. A track chooser supports keyboard selection. Mixer settings are session-local. The audio and MIDI selector uses JUCE and stores its configuration outside the project.
- Windows builds enable JUCE ASIO by default. Use **Settings → Audio / MIDI Devices**. Arrangement rows share mix controls with the Mixer. A track slot can load one explicitly selected VST3 instance and open its native or generic parameter editor for the session. Plug-in DSP routing, state persistence, third-party interoperability and ASIO hardware loopback checks remain pending.
- The VST3 browser caches scan results locally, groups by manufacturer and loads on track drop. Transport supports playback of pitched notes at a fixed source tempo and MIDI note capture into the selected track for the current session. The built-in preview instrument uses the selected audio output and track mix. Audio recording, save and plug-in signal routing remain pending.
- Startup shows progress for the workspace and built-in demo. VST3 scanning starts from the browser, runs in a separate app process, and reports progress through a local status file. Scan failures preserve the last written catalogue. The host displays each explicit track plug-in load on the slot and status bar.
- The mixer places source voicing-role, vocal-role and pitched tracks before other arrangement tracks. Tracks route to Master or either bus, with independent post-fader sends to up to eight summing returns. Cue is pre-fader and goes to the third and fourth enabled audio outputs. Routing applies to the preview instrument; plug-in processing, project persistence and physical loopback verification remain pending.
- The last verified GitHub `main` commit is `9abc25b`. Its [initial Actions run](https://github.com/yoh-nak/OSTF-Studio/actions/runs/44952075164) succeeded on Windows x64 and macOS arm64. It used the original ZIP workflow.
- `feature/release-packaging` contains the proposed installer and DMG workflow, seven-day Actions Artifact retention, and explicit manual Stable/Pre-release publication. It is based on `main` commit `9abc25b`. At handoff, the feature commits are local and await approval to push.
- The app icon is wired into JUCE's desktop bundles, the Windows window peer and Inno Setup. Windows resource and packaging checks passed. macOS bundle verification remains pending.

## Validation and remaining work

| Area | Verified | Pending |
|---|---|---|
| Core and OSTFP | Current local Windows Release build and CTest passed (2/2), including fixture parsing and mixer routing | Rerun on both current CI targets |
| Global Timeline | Core coverage for clipping, bounds and demo region order | Build and inspect the updated GUI at different window sizes on both platforms |
| Windows packaging | Current `0.1.0-startup-preview` Setup.exe and Portable ZIP generated from a local MSVC build. Portable payload hash matched. Earlier installer smoke test verified install, upgrade, uninstall and user-data preservation | Generate packages from a feature-branch CI build and verify artifact download |
| macOS packaging | Prior CI `.app` path and arm64 Mach-O header verified | Generate and verify the new DMG on macOS CI |
| Release workflow | `actionlint`, Bash syntax and local mock cases passed for release gating, missing assets and duplicate versions | Run an artifacts-only CI check. Actual Release publication requires separate authorization |
| Desktop operation | Earlier Windows build showed the demo and default piano roll. The current packaged scanner entry point rejected an invalid token without loading installed plug-ins | Verify the current UI, scan with actual plug-ins, and test audio/MIDI devices on Windows and Mac. Windows Authenticode, macOS Developer ID and notarization are unconfigured |

## Resume on another computer

1. Install the platform prerequisites and clone the repository using your own GitHub login if prompted. Open the clone in Codex and read [AGENTS.md](AGENTS.md), this handoff, and the [build instructions](README.md#build-core-tests-no-juce-download-needed).
2. Run `git fetch origin`, `git status --short --branch`, and `git branch -avv`. Continue from the verified remote feature branch if it exists. After the approved branch is pushed, a fresh clone can use `git switch --track origin/feature/release-packaging`. Otherwise the clone has only the changes already published to GitHub.
3. Build in a fresh local build directory. Git carries source and project documentation. Compilers, SDKs, credentials, plugins, personal settings and generated packages are managed separately on each machine.
4. Run the build and tests relevant to the next change, update this handoff, and commit on the feature branch. The pending CI check for this branch must use the artifacts-only workflow option. Follow [release and packaging procedures](docs/RELEASING.md) for packaging and approvals.

# OSTF Studio development handoff

Read this file after `AGENTS.md` when resuming work on another computer. Update it in the same change as significant implementations, fixes, or validation results. Record what changed, what was verified on each platform, and what remains to be done. Keep durable policy in `AGENTS.md` and detailed procedures in the linked documents.

## Current state

- OSTF Studio is a JUCE UI bootstrap. It has a demo Global Timeline with aligned track lanes, a fixed-meter C++ timeline model with tests, and basic JSON object/array inspection.
- The official OSTF 5.0 specification, schemas and samples are still pending. OSTF/OSTFP serialization, playback, plugin hosting, AI editing and remote collaboration are planned. See the [architecture and roadmap](docs/ARCHITECTURE.md).
- The last verified GitHub `main` commit is `9abc25b`. Its [initial Actions run](https://github.com/yoh-nak/OSTF-Studio/actions/runs/44952075164) succeeded on Windows x64 and macOS arm64. It used the original ZIP workflow.
- `feature/release-packaging` contains the proposed installer and DMG workflow, seven-day Actions Artifact retention, and explicit manual Stable/Pre-release publication. It is based on `main` commit `9abc25b`. At handoff, the feature commits are local and await approval to push.

## Validation and remaining work

| Area | Verified | Pending |
|---|---|---|
| Core | Local Windows CTest passed (1/1) | Rerun on both current CI targets |
| Windows packaging | Setup.exe and Portable ZIP generated locally from the prior CI binary. Install, upgrade and uninstall preserved test user data. | Generate packages from a fresh feature-branch CI build and verify artifact download |
| macOS packaging | Prior CI `.app` path and arm64 Mach-O header verified | Generate and verify the new DMG on macOS CI |
| Release workflow | `actionlint`, Bash syntax and local mock cases passed for release gating, missing assets and duplicate versions | Run an artifacts-only CI check. Actual Release publication requires separate authorization |
| Desktop operation | Compilation verified by the initial CI run | Launch and operate the GUI on Windows and Mac. Windows Authenticode, macOS Developer ID and notarization are unconfigured |

## Resume on another computer

1. Install the platform prerequisites and clone the repository using your own GitHub login if prompted. Open the clone in Codex and read [AGENTS.md](AGENTS.md), this handoff, and the [build instructions](README.md#build-core-tests-no-juce-download-needed).
2. Run `git fetch origin`, `git status --short --branch`, and `git branch -avv`. Continue from the verified remote feature branch if it exists. After the approved branch is pushed, a fresh clone can use `git switch --track origin/feature/release-packaging`. Otherwise the clone has only the changes already published to GitHub.
3. Build in a fresh local build directory. Git carries source and project documentation. Compilers, SDKs, credentials, plugins, personal settings and generated packages are managed separately on each machine.
4. Run the build and tests relevant to the next change, update this handoff, and commit on the feature branch. The pending CI check for this branch must use the artifacts-only workflow option. Follow [release and packaging procedures](docs/RELEASING.md) for packaging and approvals.

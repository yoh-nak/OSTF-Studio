# Starting work on OSTF Studio

Read AGENTS.md, HANDOFF.md, README.md, and docs/ARCHITECTURE.md before starting. HANDOFF.md records current work, verified results and open tasks. README.md tracks available features, and the architecture document records planned milestones and OSTF 5.0 intake requirements. See docs/RELEASING.md for build packages and authorized releases.

Give Codex a natural-language task with the intended behavior and any acceptance criteria. Work on a feature branch, inspect the affected code, and run the relevant local and GitHub Actions checks. Report compilation, tests, packaging, and on-device behavior separately for Windows x64 and macOS arm64. Obtain the required approval before integration into main. Treat release publication as a separate operation.

The official OSTF 5.0 specification, schemas, and samples are a prerequisite for implementing format conformance.

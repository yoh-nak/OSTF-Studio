# First prompt to paste into Codex

Develop OSTF Studio from this starter repository as a serious cross-platform DAW project.

First, read `AGENTS.md`, `README.md`, and `docs/ARCHITECTURE.md` fully. This is a scaffold, not a working DAW. Do not claim missing features are implemented.

**Your first task (one PR only):**
1. Inspect the source and build configuration.
2. Run the non-GUI CMake tests. If build errors exist, fix them.
3. Run the Windows and macOS GitHub Actions compilation if connected and authorized. Resolve real compiler errors, not hypothetical ones.
4. Improve the desktop UI skeleton while preserving resizable global-timeline layout and distinct tracks. Ensure the current demo information is explicitly labeled demo.
5. Create an implementation plan for OSTF 5.0 text import, OSTFP projects, SMF export, JUCE/Tracktion-based plugin hosting and audio engine; specify which official OSTF schema files are still required.
6. Keep this first PR focused on a successfully building, maintainable starter and truthful documentation; do NOT improvise a schema or implement untested audio/AI features.

Future architecture target: OSTF score independent of any DAW, OSTFP default project, OSTF TEXT clipboard/import/merge, global musical timeline as central coordinator, optional audio regions, Windows ASIO+WASAPI separate input/output eventually, macOS CoreAudio, VST3 and optional AU, SVP and SMF exports, AI diff approvals and remote sync. Work in small testable PRs with reproducible CI output. Do not push to `main` without review.

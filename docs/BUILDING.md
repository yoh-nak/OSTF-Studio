# Incremental builds

Run these commands from the repository root with CMake 3.22+, Ninja and Python 3 on PATH. On Windows use an x64 Visual Studio Developer PowerShell. On macOS select the Xcode toolchain for the architecture being tested.

| Task | Command |
| --- | --- |
| Build all changed targets | `python tools/build.py` |
| Work on the desktop app | `python tools/build.py --target OSTFStudio` |
| Work on the hosted workstation | `python tools/build.py --target OSTFStudioPlugin_VST3` |
| Test the core without fetching JUCE | `python tools/build.py --core --test` |
| Build and run the full test suite | `python tools/build.py --test` |
| Validate the release configuration locally | `python tools/build.py --mode release` |
| Diagnose a clean configure and build | `python tools/build.py --mode clean --test` |

Development and release use separate persistent trees. Clean mode creates a new directory and prints its path. Use `--build-dir` for a persistent tree on another drive. Keep one generator, architecture, SDK and compiler per tree. An existing Visual Studio tree can still be built with its original CMake commands. To find a device target, run `cmake --build build-dev-studio --target help`.

The helper chooses up to one compiler process per available CPU, reserves 1 GiB for the system and budgets 2 GiB of available memory per concurrent job. Set `CMAKE_BUILD_PARALLEL_LEVEL` to a positive integer for a measured override. The same job budget applies to nested CMake builds. Install sccache or ccache before configuring a new tree to use the compiler launcher. CMake and Ninja track headers, generated inputs, compile flags and dependencies even when the cache is empty.

## Dependencies and compiler caches

The score model and audio pitch/warp processing live in `ostf_studio_core`. JSON schema validation owns its jsoncons dependency. Stock instrument and effect wrappers link the shared DSP library. The Scale Editor document model is shared separately. The workstation VST3 and standalone app compile their host-specific JUCE modules with distinct definitions. Keep future SDK and analysis code behind narrow target interfaces. Add a device through its catalogue and link its wrapper against the appropriate implementation. Avoid process-wide compile definitions for device parameters or display metadata.

The release display version is generated into one translation unit linked into both workstations. Changing it relinks the consumers. Binary plug-in IDs, architecture and compiler settings still affect the targets that use them.

The build retains the pinned JUCE checkout and verifies its revision when Git metadata is present. Local archive-only mirrors are supported via `-DFETCHCONTENT_SOURCE_DIR_JUCE=...` and `-DFETCHCONTENT_SOURCE_DIR_JSONCONS=...`. `OSTF_DOWNLOAD_DIR` can point to a shared directory for checksum-verified downloads. When updating a dependency, update its pin, checksum and CI cache namespace together.

Windows x64, Apple Silicon and Intel Mac CI jobs use Ninja and sccache. Compiler objects use a namespace scoped to the platform, architecture, compiler/SDK, Release configuration and dependency versions. Source and flags are still hashed by the compiler cache. The download cache contains the pinned JUCE Git objects, jsoncons archive and the checksum-verified GM bank package. Build trees, installers and signing material are excluded.

## Release and verification

Select **OSTF Studio cross-platform build → Run workflow → none** to run all targets, tests, native installer checks and packaging without publishing. The workflow produces the Windows installer and Portable ZIP and separate macOS DMGs. Publication is a separate, explicitly authorized operation documented in [Releasing](RELEASING.md). A successful compile or cache hit does not verify hardware I/O or DAW behavior.

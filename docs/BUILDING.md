# Incremental builds

Run these commands from the repository root with CMake 3.22+, Ninja and Python 3 on PATH. On Windows use an x64 Visual Studio Developer PowerShell. On macOS select the Xcode toolchain for the architecture being tested.

| Task | Command |
| --- | --- |
| Build all changed targets | `python tools/build.py` |
| Work on the desktop app | `python tools/build.py --target OSTFStudio` |
| Work on the hosted workstation | `python tools/build.py --target OSTFStudioPlugin_VST3` |
| Test the core without fetching JUCE | `python tools/build.py --core --test` |
| Build and run the full test suite | `python tools/build.py --test` |
| Test and package a native release candidate | `python tools/build.py --mode release` |
| Diagnose a clean configure and build | `python tools/build.py --mode clean --test` |

Development and release use separate persistent trees. On a warm tree the helper starts Ninja directly. CMake reconfigures automatically when tracked inputs change. Explicit `-DNAME=VALUE` options request a configure. Clean mode creates a new directory and prints its path. Use `--build-dir` for a persistent tree on another drive. Keep one generator, architecture, SDK and compiler per tree. An existing Visual Studio tree can still be built with its original CMake commands. To find a device target, run `cmake --build build-dev-studio --target help`.

The helper chooses up to one compiler process per available CPU, reserves 1 GiB for the system and budgets 2 GiB of available memory per concurrent job. On a fresh GitHub-hosted Mac, it reserves 3 GiB of physical memory for the runner and uses the rest for the same 2 GiB-per-job budget. This avoids counting the runner’s reclaimable file cache as occupied by other applications. Set `CMAKE_BUILD_PARALLEL_LEVEL` to a positive integer for a measured override. The same job budget applies to nested CMake builds. Install sccache or ccache before configuring a new tree to use the compiler launcher. CMake and Ninja track headers, generated inputs, compile flags and dependencies even when the cache is empty.

## Dependencies and compiler caches

The score model and audio pitch/warp processing live in `ostf_studio_core`. JSON schema validation owns its jsoncons dependency. Stock instrument and effect wrappers link the shared DSP library. The Scale Editor document model is shared separately. The AI analysis provider compiles once and is linked by the desktop app, hosted workstation and connection test. The workstation VST3 and standalone app compile their host-specific JUCE modules with distinct definitions. Keep future SDK and analysis code behind narrow target interfaces. Add a device through its catalogue and link its wrapper against the appropriate implementation. Avoid process-wide compile definitions for device parameters or display metadata.

The release display version is generated into one translation unit linked into both workstations. Changing it relinks the consumers. A numeric JUCE product-version change updates the binary metadata and rebuilds the wrappers that embed it. The score model, pitch/warp code and DSP implementation have no dependency on the display version.

The build retains the pinned JUCE checkout and verifies its revision when Git metadata is present. Local archive-only mirrors are supported via `-DFETCHCONTENT_SOURCE_DIR_JUCE=...` and `-DFETCHCONTENT_SOURCE_DIR_JSONCONS=...`. `OSTF_DOWNLOAD_DIR` can point to a shared directory for checksum-verified downloads. When updating a dependency, update its pin, checksum and CI cache namespace together.

Windows x64, Apple Silicon and Intel Mac CI jobs use Ninja and sccache. Each runner builds the standalone core twice and checks that the second invocation adds no Ninja build steps. Compiler objects use a namespace scoped to the platform, architecture, compiler/SDK, Release configuration and dependency versions. Source and flags are still hashed by the compiler cache. The macOS document-type plist is a configure input. Distribution notices and the optional GM payload are dependencies of the bundles that ship them. Verified GM payloads retain their timestamps on a warm release build. The download cache contains the pinned JUCE Git objects, jsoncons archive and the checksum-verified GM bank package. Build trees, installers and signing material are excluded.

## Release and verification

Local release mode runs the full CTest suite and writes the native packages to the `packages` directory inside its build tree. Windows requires Inno Setup 6. macOS requires Xcode and the system package tools.

Select **OSTF Studio cross-platform build → Run workflow → none** to run all targets, tests, native installer checks and packaging without publishing. The workflow produces the Windows installer and Portable ZIP and separate macOS DMGs. Publication is a separate, explicitly authorized operation documented in [Releasing](RELEASING.md). A successful compile or cache hit does not verify hardware I/O or DAW behavior.

## Windows measurements

A display-version-only change on the same warm Windows x64 Release tree (MSBuild, four jobs, `OSTFStudio` target) previously compiled 74 C/C++ sources in 466.88 seconds. With the generated version unit, the same operation compiled one file in 30.50 seconds, including relinking. An immediate repeat compiled zero files in 11.96 seconds. Times exclude configure. To reproduce, warm the target, change only `-DOSTF_STUDIO_DISPLAY_VERSION:STRING=timing-test`, then time `cmake --build <tree> --config Release --target OSTFStudio --parallel 4`. Count compiler input filenames in the MSBuild output, or compilation entries in Ninja’s log. Restore the normal version with `-UOSTF_STUDIO_DISPLAY_VERSION`. The one-time migration build took 653.80 seconds and compiled 77 sources, including changed application code and generated resources. A second invocation of the Ninja development helper on the core reported `no work to do` and passed both core tests.

The precompiled-header experiment built the two production sources of `ostf_studio_core` in fresh Ninja Release trees using MSVC 19.44 and two compile jobs. Three runs without a PCH took 20.23, 6.02 and 10.02 seconds. With `<algorithm>`, `<cmath>` and `<vector>` precompiled they took 11.34, 9.77 and 11.84 seconds. Medians were 10.02 and 11.34 seconds respectively. Another workstation build was active during the experiment. The core keeps ordinary includes. Before introducing a GUI or device PCH, measure that target separately with its real compiler definitions.

## CI measurements

The table shows wall time for the GUI workflow’s **Build** step. Configure, tests and packaging are separate steps. The earlier run built the public-alpha-2 candidate with two jobs per platform. The first optimized run built the boundary-edit changes and could reuse compiler entries from earlier PR runs. Source revisions and cache warmth differ. Use the controlled Windows display-version experiment above for a like-for-like comparison. [Earlier run](https://github.com/yoh-nak/OSTF-Studio/actions/runs/46035768915) · [First optimized run](https://github.com/yoh-nak/OSTF-Studio/actions/runs/46059771237)

| Runner | Earlier build | First optimized build | Jobs | Cache hits / actual compilations |
| --- | ---: | ---: | ---: | ---: |
| Windows x64 | 30m 50s | 18m 50s | 4 | 275 / 520 |
| Intel Mac | 36m 33s | 24m 00s | 4 | 465 / 453 |
| Apple Silicon | 20m 54s | 32m 08s | 1 | 435 / 483 |

All three caches reported zero errors. The Apple Silicon result prompted the dedicated-runner memory rule above. On a 7 GiB hosted Mac with at least two CPUs it allows two compiler jobs. Local macOS development continues to use currently available memory.

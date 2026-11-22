# OSTF Studio

**1.0.0 alpha** is the first public testing series for Windows x64 and Apple silicon Macs. Try a bundled demo to explore the Global Timeline, piano roll, VST3 instruments and session mixer. [Alpha downloads and release notes](https://github.com/yoh-nak/OSTF-Studio/releases)

OSTF Studio is intended to become an AI-native DAW with OSTF 5.0 as canonical musical score, OSTFP as the project format, a powerful global musical timeline, VST3/AU hosting, optional audio assets, AI arrangement, and future distributed sessions.

## ☕ Buy Me a Coffee で開発を応援する

OSTF Studioは現在も開発を続けています。デモ曲を聴いて「このDAWで曲を作ってみたい」と感じたら、コーヒー1杯分のご支援をいただけるとうれしいです。GM音源の音づくりや、Windows・macOSの両方で安心して曲を開いて再生できるよう、改良と検証を続ける励みになります。

OSTF Studio is in active development. If a demo makes you want to write a song with it, buying me a coffee helps me keep refining the GM instruments and testing playback on Windows and macOS.

**[☕ Buy Me a Coffee で支援する / Support OSTF Studio](https://buymeacoffee.com/yohsukenak)**

## This starter contains
- Portable C++20 timeline model with unit tests (internal only; not OSTF spec definitions)
- JUCE 9.0.5 desktop window with a **32-bar demo** ruler, global musical lanes, and named arrangement regions aligned to the same bar positions
- `.ostf` and `.json` **basic JSON object/array inspection** using JUCE, without OSTF schema validation
- **OSTF 5.0.0 and OSTFP 1.0** support a bounded score and mixer subset. **Open demo** loads the bundled official 16-bar song. **Open OSTF / OSTFP** opens supported files from disk.
- Select an arrangement row to view its pitched notes in the read-only piano roll. The ruler shows song bar numbers and the keyboard follows the track's pitch range. Sound-slot percussion is outside the pitched view.
- The official demo opens on startup with a pitched track selected. The transport sends pitched notes to assigned VST3 instruments. Enable a MIDI input and use **Record MIDI** to capture notes into the selected track. Recording starts at the beginning, and Stop turns the captured notes into a clip that can be saved. Audio recording is pending.
- Use **File** to open a project or demo, **View** to switch editors, and **Settings → Audio / MIDI Devices** to select devices. The public 1.0.0-alpha.1 packages use **Windows Audio (WASAPI)** on Windows x64 and **Core Audio** on Apple silicon Macs. Windows ASIO support is planned for a later alpha after licensing.
- Arrangement rows have a horizontal volume fader, mute, solo, and an **Instrument** slot. **Mix** opens track-local volume, pan, output bus and return-send controls backed by the same mixer state. The row and Mix controls display the preset name when the plug-in reports it.
- The startup screen shows workspace preparation. **Plug-ins** opens a manufacturer-grouped VST3 browser. The first opening without a cache starts the scanner in a separate process and shows the current plug-in and scan progress. Subsequent openings use the saved local XML catalogue. Drag a plug-in onto an arrangement row to instantiate it without a new directory scan. **Scan VST3** checks for additions or changed files. Track slots display when an individual plug-in is loading.
- The Mixer puts voicing-role, vocal-role and pitched tracks on the left. Source chord voicings are shown as Global Timeline context when present. Use **Out** for Master or Bus A/B, **Send** for per-return send levels, and **+ Return** to add up to eight returns. Buses and post-fader returns sum to Master. The pre-fader **C** switch sends to Cue, using the third and fourth enabled output channels. Cue remains silent when fewer than four outputs are active. These paths apply to the instrument's audio return. Insert and send effects are pending.

### Global note tracks

OSTF Core stores chord voicing snapshots under `harmony.chords[].voicing` and performed notes under `tracks[].clips[].notes`. The **Chord Voicing** row displays note names and derives preview notes over each chord span. It has a mixer channel with its own instrument slot. Its channel settings are stored in the Studio extension without adding a Core source track. This derived row is the only global voicing row. A performed part such as **Keys Voicing** appears under **Arrangement Tracks**, even when its role is `voicing`. Tracks with the `melody`, `lead`, `vocal` or `vocals` role still appear above **Arrangement Tracks**. All these note rows open the piano roll and expose mute, solo, volume and pan.

### Shared instruments and OSTF GM Sound Module

Load **OSTF GM Sound Module** on one arrangement track. The module displays all 16 MIDI channels and their GM names, with channel 10 reserved for percussion. Select a channel to change its program, volume, pan, expression, tuning, pitch-bend range, sustain, mute or solo. Incoming Program Change and the supported MIDI controls update the panel. The module saves these part settings in its VST3 state.

Distribution builds use **FluidR3 GM** as the factory SoundFont. The bundle includes its MIT redistribution notice. The sample tables are shared by instances of the module. **Load SoundFont** still accepts a local SF2, and **Factory bank** returns to the distributed bank. For a development build with FluidR3, run `python packaging/prepare_fluid_r3.py` before configuring CMake. CI downloads the pinned Debian package and verifies its SHA-256. A build without the downloaded resources uses the original synthesized Studio Preview GM bank. [Debian package](https://packages.debian.org/sid/sound/fluid-soundfont-gm) · [Redistribution notice](https://sources.debian.org/src/fluid-soundfont/3.1-6/debian/copyright/)

On another track, choose **Instrument → Instrument destination** and select the track holding the module. Select the intended **MIDI channel** in that track's menu. Repeat for each part. Preview playback routes up to 16 Studio tracks to channels 1–16. The shared stereo audio, fader, mute, solo, bus and cue are controlled by the track holding the instrument. Per-part faders and individual audio outputs are pending. Track assignments are session-local.
- Tracks and mixer outputs scroll independently, with buses, returns, Master and Cue grouped on the right. **View → Global Timeline Lanes** controls the visible OSTF 5.0 lanes and saves that preference locally. Optional lanes show supported bar-aligned data when it exists in the score. Legacy demo fields are excluded from the menu.
- The VST3 browser can cancel a scan and retain saved entries. If a plug-in stalls for 25 seconds, scanning skips that plug-in and resumes, with a bounded retry count.
- Use **Instruments** or **Effects** in the plug-in browser to filter the manufacturer list. A keyboard icon marks instruments and a waveform icon marks effects. Drag a plug-in over an arrangement row and release when the row lights up to assign it to that track.
- If an installed VST3 is missing, choose **Scan one…** and select its `.vst3` file or bundle. This retries only the selected plug-in, including a previously skipped one. Windows commonly installs VST3 files under `C:\Program Files\Common Files\VST3`. Legacy VST2 DLLs are unsupported.


In OSTF Studio, open a supported `.ostfp` and drag the name of an arrangement track with pitched notes onto a MIDI track in another DAW. **Drag MIDI to another DAW**, at the right edge of the Piano Roll toolbar, does the same for the selected track. The Standard MIDI File keeps the notes' project-relative start, duration and pitch and uses a fixed velocity. The source project stays in OSTF format.

When Studio opens **OSTF Arranger**, the editor also displays **Drag notes to an OSTF Studio track**. Drop it at the desired bar to add the pattern to the current session. The VST3's **Drag MIDI clip to a DAW track** control exports that pattern as a MIDI file for external hosts.

### OSTF Arranger and piano-roll performance lanes

In Studio, drag **OSTF Studio → OSTF Arranger** from the Plug-in Browser to the regular Instrument slot. In an external DAW, load **OSTF Arranger** on an instrument track. In either host, open the Arranger editor and click **Choose instrument**. It lists VST3 instruments from the OSTF Studio scan cache by manufacturer. Use **Browse for VST3 file** for an unlisted instrument and **Edit instrument** to open the loaded instrument controls. The preview generates C–E–G–E from the host transport and returns stereo audio. The plug-in state stores the hosted instrument reference and settings. The hosted instrument must remain installed. The preview hosts one mono or stereo MIDI-capable VST3 instrument without audio inputs. External DAW interoperability remains to be tested.

Arrangement clips show the number of stored notes. In **Piano Roll**, the performance selector shows authored velocity and, when present, individual CCs, channel/polyphonic aftertouch and vocal pitch and expression curves by voice. **Paper Lantern Replies** has example CC11, channel pressure, pitch and dynamics on **Lead Vocal Guide**. CC, aftertouch and vocal curves are read-only visualizations. Velocity drives pitched-note preview playback and MIDI export. An unspecified source velocity stays unspecified in the read model.

### OSTF and OSTFP files

**File → Open OSTF / OSTFP** opens supported OSTF 5.0 scores and projects in the same arrangement and piano-roll views. A plain score restores Studio settings when its Studio extension is present. The preview currently requires one meter and at most 256 bars.

**Save as OSTF** writes the score and Studio mixer settings. If tracks have linked WAVs, it creates a sibling `<song>.media` folder and copies the WAVs there. The score references those copies by relative path. Move the score and media folder together. **Save as OSTFP** bundles the score, the Production Session 0.3 mixer document and the linked WAVs. Use **Instrument → Link WAV on this track…** to link one WAV per track for playback from the song start. The preview supports WAVs of up to 16 MiB each and up to eight linked files. Under **Output Tracks**, each bus, return and Master has its own arrangement lane with a fader and mute synchronized with the Mixer. Open **FX** on a source or output track to see up to eight stereo VST3 effects in processing order. Drag a row handle or use **Up** and **Down** to reorder it. Use the same panel to add, open or remove effects. Dropping an effect onto a track appends it. Source effects process instrument and WAV audio in order before the fader and post-fader sends. Output effects process the summed signal before the output fader. Changing a chain stops preview playback. Both save formats preserve these effect assignments and supported mixer and MIDI settings. Supported VST3 instrument assignments are saved as Production Session devices in OSTFP and in the common Studio production settings for standalone OSTF. Core MIDI Program Change uses the authored channel or the track channel. Instrument preset blobs and hardware settings remain outside this save implementation.

Use **Track** to add a pitched, drum/percussion or audio track, or to delete the selected track after confirmation. Audio tracks have a **Link WAV** control. The preview supports up to eight arrangement tracks. Drum-grid editing and recording on drum tracks are pending.

Windows Setup registers a document icon for `.ostf` and a package icon for `.ostfp`. If another default app is already set, select **Open with → Choose another app → OSTF Studio → Always**. The Portable ZIP does not register file types. The macOS app bundle declares matching document icons.

Windows Setup installs the VST3s under `%LOCALAPPDATA%\Programs\Common\VST3`. In Ableton Live, enable the VST3 custom folder and point it there if the installed plug-in does not appear after a rescan. The Portable ZIP includes the VST3 bundles for manual placement.

The macOS arm64 workflow places a combined app and VST3 installer in the DMG. Signing and notarization are pending.
- GitHub Actions Windows + macOS **preview** builds, unsigned
- `AGENTS.md` instructions for continuing in Codex
- Architecture/roadmap/checklists

Pending work includes full OSTFP conformance, SVP export, network sync, AI connection and signed binaries.

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

For Windows, use the `Visual Studio 17 2022` generator with `-A x64` for incremental builds, as in CI. When using MSVC/Ninja, check that `ninja -t deps` lists the project's headers before relying on incremental output. If localized compiler include lines are unrecognized, use the Visual Studio generator in a fresh directory. The Windows GUI CTest runs a startup smoke test through the splash screen.

## Build outputs
The [build workflow](.github/workflows/build.yml) runs standalone core tests and builds the JUCE GUI on Windows x64 (Visual Studio 2022) and macOS arm64. Successful jobs upload a Windows installer and Portable ZIP, and a macOS DMG, under **Actions > run > Artifacts** for seven days. Publishing a stable release or pre-release requires an explicit workflow dispatch on `main`. See [Packaging and releases](docs/RELEASING.md) for version tags, installation behavior and macOS signing requirements. GUI interaction and audio-device behavior require separate real-device validation.

See the [development roadmap](docs/ARCHITECTURE.md#user-visible-milestones) for the official OSTF 5.0 intake requirements and the sequence for implementing DAW features.

## Important: OSTF 5.0 spec

The [original demo song gallery](examples/demo-songs/README.md) provides five OSTF/OSTFP songs and a per-song checklist for assessing current Studio support.
The preview checks package inventory and hashes before displaying the supported source lanes. It currently requires a single meter and bar-aligned sections, and supports a score with an optional Studio mixer document. Full Schema validation in the application, mixed meters, and sub-bar timing for other global lanes remain pending. Packaged WAVs linked by OSTF Studio are supported. [Official OSTF 5.0.0 release](https://github.com/yoh-nak/Open-Song-Timeline-Format-5.0/tree/d8ce48ca6c873a5a24a94bcc80d847bf33135b46) · [Fixture provenance](tests/fixtures/README.md)

## Licensing
JUCE is subject to its own licensing; do not assume donation-supported freeware is automatically exempt. Review the licensing and redistribution conditions for the final project before public release. No separate OSS licence is assigned to user-developed files in this starter.

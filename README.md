# OSTF Studio

OSTF Studio is a desktop music workstation for arranging, playing and editing Open Song Timeline Format scores.

![OSTF Studio Live Session showing the section queue, nine tracks and the vocal piano roll](docs/images/ostf-studio-live-session.png)

*Development preview · Paper Lantern Replies.* The session grid shows which takes will play in each section. The chord guide shows the current and upcoming chords, and the piano roll follows the selected track.

**English** · [日本語](README.ja.md)

**Download 1.0.0 alpha 3** · [Windows x64 installer](https://github.com/yoh-nak/OSTF-Studio/releases/download/v1.0.0-alpha.3/OSTF_Studio_Windows_x64_Setup.exe) · [Windows portable ZIP](https://github.com/yoh-nak/OSTF-Studio/releases/download/v1.0.0-alpha.3/OSTF_Studio_Windows_x64_Portable.zip) · [Apple silicon Mac DMG](https://github.com/yoh-nak/OSTF-Studio/releases/download/v1.0.0-alpha.3/OSTF_Studio_macOS_arm64.dmg)

**1.0.0 alpha** is the first public testing series for Windows x64 and Apple silicon Macs. Try a bundled demo to explore the Global Timeline, piano roll, VST3 instruments and session mixer. [Alpha downloads and release notes](https://github.com/yoh-nak/OSTF-Studio/releases)

OSTF Studio is intended to become an AI-native DAW with OSTF 5.0 as canonical musical score, OSTFP as the project format, a powerful global musical timeline, VST3/AU hosting, optional audio assets, AI arrangement, and future distributed sessions.

## ☕ Support development on Buy Me a Coffee

OSTF Studio is in active development. If a demo makes you want to write a song with it, buying me a coffee helps me keep refining the GM instruments and testing playback on Windows and macOS.

**[Buy me a coffee](https://buymeacoffee.com/yohsukenak)**

## Opening older scores in alpha 3

Use **File → Open** to open a standalone OSTF 3.5.0, 4.0.0, 4.1.0, 4.2.0, 4.3.0 or 5.0.0 score directly. Opening an older song leaves the source file intact. **Save as OSTF** and **Save as OSTFP** write a 5.0.0 score and initially suggest a new filename. The preview supports one meter, up to eight source tracks and up to 256 bars.

## Transport and song information in alpha 3

The transport shows the active section, tempo, meter, key, scale and chord alongside bar/beat/tick and 30 fps timecode. **Home** returns to the beginning. **Rewind** and **Forward** move one bar per click and repeat when held. Stop parks the playhead and Play resumes there. At the end of the song, Play starts again from the beginning.

**Follow** is on by default. As the playhead approaches the right edge, the arrangement and piano roll scroll at their own zoom levels. Turn Follow off to inspect another passage while playback continues. Turning it back on brings the playhead into view. The view toolbar shows the song title and filename.

Open **File → Song information** to edit the title, subtitle, description and ordered credits. Enter a contributor name and role for each credit (for example, `composer`, `lyricist` or `arranger`). Apply the changes, then save as OSTF or OSTFP. Both formats retain the credit assignments.

## Notation and import in alpha 3

The first three buttons on the view bar switch the main workspace between **Arrangement**, **Lead sheet** and **Score**. The View menu opens the same modes. Playback and the transport stay available as you switch. The moving gold line marks the current position. **Follow playback** advances the bars in view. Turn it off to browse another section during playback.

In the lead sheet, switch chords, scales and lyrics on or off independently. Choose 1, 2, 4, 8 or 16 bars per line. For songs with timed lyrics, select the voice and display text, pronunciation or both. Pronunciation comes from the OSTF lyric unit, with its text as a fallback. In Score, choose a track and its treble, alto, bass, grand staff, guitar TAB, bass TAB or percussion notation. **PDF / Print** lays out the chosen content on A4 pages for printing. **MusicXML** exports the selected score part for a notation editor.

**File → Import and transcription** creates a new project from a MIDI performance (SMF 0 or 1) or a WAV recording. WAV import keeps the recording alongside the score. The experimental melody mode adds draft notes from up to two minutes of isolated single-note audio. Review the result in the piano roll.

## Install OSTF Studio

On Windows, run the Setup executable or extract the portable ZIP. On an Apple silicon Mac, open the DMG and run **Install OSTF Studio and VST3.pkg**. Public installers and matching source archives are listed under **Assets** on each [GitHub Release](https://github.com/yoh-nak/OSTF-Studio/releases). Development packages are available for seven days under **Actions → build run → Artifacts**.

### MIDI export, language and view controls

- Choose **File → Save as MIDI (SMF)** for a song-wide `.mid` file. Format 0 merges the parts into one track while retaining their MIDI channels. Format 1 writes a conductor track and named part tracks. Both preserve the source resolution, meter, tempo changes, notes, mapped GM percussion and supported channel events. Audio, plug-in state and vocal expression curves stay in the OSTF project. The derived Chord Voicing display does not create an extra MIDI part.
- **Settings → Language** offers the system language, English and Japanese. The selection takes effect on the next launch. An unsupported system language uses English.
- The icons below the menu bar open or hide the Piano Roll, Mixer, Plug-in Browser and Output Tracks. The lanes icon chooses which Global Timeline lanes to display. The View menu provides the same controls.

## This starter contains
- Portable C++20 timeline model with unit tests (internal only; not OSTF spec definitions)
- JUCE 9.0.5 desktop window with a **32-bar demo** ruler, global musical lanes, and named arrangement regions aligned to the same bar positions
- Standalone `.ostf` scores open in the workspace. The `.json` inspector accepts generic JSON objects and arrays.
- **OSTF 5.0.0 and OSTFP 1.0** support a bounded score and mixer subset. **File → Demo songs** opens the Studio gallery, including Copper Mile (Rock) and First Light Vector (Trance). **Open OSTF / OSTFP** opens supported files from disk.
- Select an arrangement row to view its pitched notes in the read-only piano roll. The ruler shows song bar numbers and the keyboard follows the track's pitch range. Sound-slot percussion is outside the pitched view.
- Studio starts with an untitled, empty 32-bar project in 4/4 at 120 BPM. Add the first part from the Track menu. **File → New project** opens another blank session. The transport sends pitched notes to assigned VST3 instruments. Enable a MIDI input and use **Record MIDI** to capture notes into the selected track from the current position. The navigation keys become available again when you stop recording. Stop turns the captured notes into a clip that can be saved. Audio recording is pending.
- Use **File** to open a project or demo, **View** to switch editors, and **Settings → Audio / MIDI Devices** to select devices. The Windows x64 alpha supports **Windows Audio (WASAPI)** and **ASIO** with a compatible manufacturer driver installed. Apple silicon Macs use **Core Audio**.
- Arrangement rows have a horizontal volume fader, mute, solo, and an **Instrument** slot. **Mix** opens track-local volume, pan, output bus and return-send controls backed by the same mixer state. The row and Mix controls display the preset name when the plug-in reports it.
- The startup screen shows workspace preparation. **Plug-ins** opens a manufacturer-grouped VST3 browser. The first opening without a cache starts the scanner in a separate process and shows the current plug-in and scan progress. Subsequent openings use the saved local XML catalogue. Drag a plug-in onto an arrangement row to instantiate it without a new directory scan. **Scan VST3** checks for additions or changed files. Track slots display when an individual plug-in is loading.
- The Mixer puts voicing-role, vocal-role and pitched tracks on the left. Source chord voicings are shown as Global Timeline context when present. Use **Out** for Master or Bus A/B, **Send** for per-return send levels, and **+ Return** to add up to eight returns. Buses and post-fader returns sum to Master. The pre-fader **C** switch sends to Cue, using the third and fourth enabled output channels. Cue remains silent when fewer than four outputs are active. Tracks that share one instrument show its combined audio level on the track hosting the instrument.

### Level meters and track height

In alpha 3, each track header and mixer strip shows left and right sample peaks after its effects, pan and fader. The mixer also meters buses, returns, Master and the independent Cue output. The red indicator holds for one second when the signal reaches 0 dBFS.

At the bottom of the arrangement, choose a track type beside **Height** and drag the adjacent slider to resize the group. You can target all tracks, a section, or a type such as drums, buses or returns. **Mixed** means the rows currently have different heights. The arrangement and piano roll each have a **Zoom** slider. Drag right to zoom in, double-click to fit the song, and use the adjacent scrollbar to move through it. Drag the lower edge of a track to change its height individually. Double-click the edge to fold the track or restore its previous height. Compact rows keep mute, solo, FX and metering within reach. Taller rows show more instrument and mix controls and display the notes inside their clips. Use the vertical scrollbar to reach the remaining tracks.

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

In alpha 3, the shared VST3 task in Windows Setup requests administrator approval to install both instruments and all nine effects in `%CommonProgramFiles%\VST3\OSTF Studio`. Other DAWs can find them with their system-folder scan. Setup keeps matching, versioned plug-in binaries in place and installs missing files. If a DAW is using a binary that needs updating, you can install Studio first. Save your session, close the DAW, and rerun Setup with the shared VST3 task selected to finish the update. After a successful shared install, recognized copies from older per-user installs move to `%LOCALAPPDATA%\OSTF Studio\Previous VST3`. The shared plug-ins have their own uninstaller. The Portable ZIP keeps them beside the app.

On macOS arm64, run **Install OSTF Studio and VST3.pkg** inside the DMG to install the plug-ins in `/Library/Audio/Plug-Ins/VST3`. Dragging just the app installs the application alone. Signing and notarization are pending.
- GitHub Actions Windows + macOS **preview** builds, unsigned
- `AGENTS.md` instructions for continuing in Codex
- Architecture/roadmap/checklists

Pending work includes full OSTFP conformance, SVP export, network sync, AI connection and signed binaries.

## Stock effects and mixed demos in alpha 3

Alpha 3 includes nine stereo VST3 effects by OSTF Studio. Open **File → Demo songs → Paper Lantern Replies** and inspect the lead vocal's **EQ → Compressor** chain. Drums and bass share Bus A. Accompaniment shares Bus B. Individual post-fader sends feed a fully wet reverb on Return 1, which returns to Master. Both file formats recall the mix. The demo gallery lists starting points for listening and testing each song. [Demo mixes](examples/demo-songs/README.md)

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
OSTF Studio's original application and bundled plug-in code is available under **GPL-3.0-only**. Distribution uses the GPLv3 option for the ASIO SDK and AGPLv3 for JUCE. Each release includes the matching source and build scripts. [GPLv3](LICENSE) · [Dependency licenses and source](licenses/NOTICE.md)

# OSTF Studio

**English** · [日本語](README.ja.md)

Choose AI Arrangement Studio, AI Lyric Studio or AI Vocal Studio from the **Mode** menu. The prompt guide beside the global timeline headings cycles through three example requests for the selected mode every 15 seconds. The previous and next buttons reset the reading time. The ChatGPT connection button sits at the right of the top toolbar, alongside the model selector and refresh button when signed in. Open **Settings → Mackie Control** to connect a control surface. Drag an analysed part onto a compatible track or into the arrangement to create one. Its notes extend the song as needed. Melody track bodies open at 100 px.

The **External connections** menu opens Splice, Melodyne and Ableton Link. Select an audio clip in the arrangement or the lower clip lane to open its source range in Melodyne. Opening Melodyne without a selection launches the standalone editor. Enable Link in both Live and Studio and enable Start Stop Sync in Live to share transport. The Link tempo menu shows the connected peer count. Audio capture holds its starting tempo for the duration of a take.

**Download 1.0.0 alpha 3** · [Windows x64 installer](https://github.com/yoh-nak/OSTF-Studio/releases/download/v1.0.0-alpha.3/OSTF_Studio_Windows_x64_Setup.exe) · [Windows portable ZIP](https://github.com/yoh-nak/OSTF-Studio/releases/download/v1.0.0-alpha.3/OSTF_Studio_Windows_x64_Portable.zip) · [Apple silicon Mac DMG](https://github.com/yoh-nak/OSTF-Studio/releases/download/v1.0.0-alpha.3/OSTF_Studio_macOS_arm64.dmg)

**1.0.0 alpha** is the first public testing series for Windows x64 and Apple silicon Macs. Try a bundled demo to explore the Global Timeline, piano roll, VST3 instruments and session mixer. [Alpha downloads and release notes](https://github.com/yoh-nak/OSTF-Studio/releases)

OSTF Studio is intended to become an AI-native DAW with OSTF 5.0 as canonical musical score, OSTFP as the project format, a powerful global musical timeline, VST3/AU hosting, optional audio assets, AI arrangement, and future distributed sessions.

## Mackie Control

Set the device to Mackie Control (MCU) mode and select its DAW MIDI input under **Settings → Mackie Control**. Select the feedback output to mirror faders, pan rings and button LEDs. The eight strips control volume, pan, record arm, solo, mute and selection. Bank and channel navigation move the strips across the mixer. Transport controls include rewind, forward, stop, play and record. Double-click a track name to edit it in place, press Enter to save or Escape to cancel.

Arm a MIDI track to play its assigned instrument while stopped and see its post-fader audio level. The track instrument menu's **MIDI OUT** destination sends live and sequenced MIDI to a hardware port. A separate audio input can receive the hardware's sound. Port selections are local to this computer.

Audio loops show a seam at each repeat. Editing a warp marker in one repetition updates the same point in every repetition. Extending a one-shot clip adds silence outside its source. To loop a MIDI phrase, right-click its clip to split at the pointer or cursor, enable **Loop clip**, then drag the right edge or choose **Add one repeat**. Studio writes repeated notes and MIDI events into the score and stores the period in a Studio extension.

## Recording in the lower editor (development build)

For a pitched or drum track, use **IN** to choose the MIDI input port and channel. Play a note to check the green input lamp. The audio output meter shows the level produced by the instrument. Press **R** on the destination track and then the transport record button. Only one track is armed at a time. The lower editor’s **Record** button also arms its track. Audio tracks display the waveform without fit lanes.

Open a pitched, drum or audio track. Click the editor ruler to park the cursor. Shift-drag on the piano or waveform ruler to select a cycle, or enable **Loop range** and drag. The step view uses its existing bar-range control. **Record** starts at the cursor. **Stop recording** finalizes the clip and closes the editor. Click the metronome face to toggle the click. Use its arrow to set the recording count-in to Off or 1–8 bars. The song-end button beside fast-forward locates the end of the project. MIDI passes overdub into one clip. Audio uses the first one or two enabled input channels and records a single WAV. On each pass, newly captured audio replaces that part of the current take. Replacing an existing clip requires confirmation. The per-clip WAV size limit is 16 MiB.

In slot steps the first pad appears at the bottom. Select a pad, assign its output note, and use **MIDI Learn** to bind the next incoming note to it. Enable the controller in Audio / MIDI Settings. A strike lights the pad and its row while stopped. **Move up/down** changes the kit order. The mapping stays on this computer. New views show up to ten bars, with 100-pixel arrangement lanes and compact global and output lanes.

## Import analysis (development build)

Use **File → Import and transcription** to analyse audio, MIDI or a PDF score. Review the musical parts in the result window, drag an individual part into the arrangement, or open the complete score in the current window. Long recordings can extend past bar 256. Audio analysis uses a provisional 120 BPM, 4/4 grid. The import limits of 60,000 notes and 15 source tracks still apply.

**Local analysis** runs offline. Local PDF recognition calls installed copies of Audiveris and MuseScore 4. For **AI analysis**, use **Connect ChatGPT** in the global track header or **Settings → AI connections**. Studio uses the official Codex CLI and its ChatGPT browser sign-in. The first connection opens a dedicated Studio sign-in. If the browser does not appear, click **Open sign-in page**. The analysis selector lists connected providers. Choose the AI mode before selecting a file. Audio sends pitch, timing and timbre measurements, MIDI sends musical summaries, and PDF sends rendered score pages. Model estimates can be corrected after import. Disconnecting affects Studio's connection only. [ChatGPT sign-in](https://developers.openai.com/codex/auth) · [Audiveris CLI](https://audiveris.github.io/audiveris/_pages/guides/advanced/cli/)

## Audio clip editing (development build)

Select an audio track to open its waveform below the arrangement. Enable **Loop** and drag the clip’s amber right edge to repeat it. **Warp (re-pitch)** fits one cycle to the chosen number of quarter notes. Double-click the waveform to add or remove a marker and drag it to move that point in time. Pitch follows the change in speed. Use **Splice Sounds** in the right plug-in pane to open the installed official VST3 editor, browse your library, audition sounds and drag a licensed WAV onto an audio track. The pane includes installation and scanning if Studio has not found the instrument. [Splice plugin and plan details](https://support.splice.com/en/articles/12997860-splice-sounds-plugin-faq)

Press **Melodyne** in the waveform editor to open the selected source as a working WAV in Melodyne standalone. Use **Replace Audio** in Melodyne, then **Apply edited WAV** in Studio to keep the edited audio in your project. The Theory Guide groups cadences directly on the degree row. The function row stays available underneath.

## OSTF Wave Analyzer (development build)

Insert **OSTF Wave Analyzer** on a track or master bus. It shows left and right waveforms, a 4096-sample spectrum, sample peak and RMS, stereo correlation, momentary/short-term/integrated LUFS and maximum 4× interpolated true peak. Hover over the spectrum for frequency and level. Freeze holds the display while measurement continues. Reset meters starts a fresh measurement on the next audio block. The plug-in runs locally and passes its input to the output with zero added latency.

## ☕ Support development on Buy Me a Coffee

OSTF Studio is in active development. If a demo makes you want to write a song with it, buying me a coffee helps me keep refining the GM instruments and testing playback on Windows and macOS.

**[Buy me a coffee](https://buymeacoffee.com/yohsukenak)**

## Opening older scores in alpha 3

Use **File → Open** to open a standalone OSTF 3.5.0, 4.0.0, 4.1.0, 4.2.0, 4.3.0 or 5.0.0 score directly. Opening an older song leaves the source file intact. **Save as OSTF** and **Save as OSTFP** write a 5.0.0 score and initially suggest a new filename. The preview supports up to fifteen source tracks.

## Transport and song information in alpha 3

The horizontal **Song defaults** strip beside or below the transport provides quick edits. **Details** opens the complete song information, credits and musical settings even in a narrow window. Select the **BPM** lane to open the tempo editor below the arrangement. Click to set a tempo or drag between two positions to draw an acceleration or slowdown. All colour themes use the same degree and note-underline palette. Local key and scale events apply in their own ranges. **Transpose or change tempo of score** shifts pitched notes and harmony and scales every tempo change. Use **Undo score edit** to revert. **Playback and display overrides** lets you audition another key or speed before committing to an edit. Opening a song resets the audition settings. Recorded audio plays at its native pitch and speed.

The Theory Guide toolbar button toggles the arrangement analysis. Arrangement tracks start at 100 px. Each visible chord-fit or scale-fit row adds a fixed 16 px and keeps that height when the track is resized. Track headers distinguish pitched instruments, drums and audio with keyboard, drum and waveform icons. The piano roll shows chord names under the bar ruler. Select or drag a pitched note with harmony colouring enabled to read its chord and scale percentages. A sustained note is evaluated over the time it spends on each side of a harmony change.

The transport shows the active section, tempo, meter, key, scale and chord alongside bar/beat/tick and 30 fps timecode. **Home** returns to the beginning. **Rewind** and **Forward** move one bar per click and repeat when held. Stop parks the playhead and Play resumes there. At the end of the song, Play starts again from the beginning.

**Follow** is on by default. As the playhead approaches the right edge, the arrangement and piano roll scroll together at the same zoom level. Turn Follow off to inspect another passage while playback continues. Turning it back on brings the playhead into view. The window title shows the song title, filename and unsaved status.

Open **File → Song information** to edit the title, subtitle, description and ordered credits. Enter a contributor name and role for each credit (for example, `composer`, `lyricist` or `arranger`). Apply the changes, then save as OSTF or OSTFP. Both formats retain the credit assignments.

## Notation and import in alpha 3

The first three buttons on the view bar switch the main workspace between **Arrangement**, **Lead sheet** and **Score**. The View menu opens the same modes. Playback and the transport stay available as you switch. The moving gold line marks the current position. **Follow playback** advances the bars in view. Turn it off to browse another section during playback.

In the lead sheet, switch chords, scales and lyrics on or off independently. Choose 1, 2, 4, 8 or 16 bars per line. For songs with timed lyrics, select the voice and display text, pronunciation or both. Pronunciation comes from the OSTF lyric unit, with its text as a fallback. In Score, choose a track and its treble, alto, bass, grand staff, guitar TAB, bass TAB or percussion notation. **PDF / Print** lays out the chosen content on A4 pages for printing. **MusicXML** exports the selected score part for a notation editor.

**File → Import and transcription** opens a separate score review. Audio analysis estimates pitched notes, percussion hits and major/minor triads locally on a provisional 120 BPM, 4/4 grid. Melody, accompaniment, bass and percussion are estimated roles. Where an isolated tone consistently matches a Studio GM family’s harmonic balance, the part name also includes an instrument-family candidate. MIDI retains its track and channel groupings. Drag an individual part or chord progression to a compatible arrangement lane. Dropping a part on **Drop to add a new track** creates its destination. Choose **Open score here as a new unsaved song** to edit all results in the original window. Audio decoding uses the registered JUCE formats, including WAV, AIFF and FLAC. PDF import uses the local OMR tools or a connected analysis provider.

New drum tracks have 16 GM assignments. In the drum editor, choose a pad and a MIDI note to remap it, or add and remove pads. The step editor shows kit order and a paged 4×4 or 8×8 surface. The piano roll shows mapped pitches in descending order. Record from an enabled MIDI keyboard or pad in either view. The on-screen pads also work during MIDI recording. Click a step cell to enter a hit while stopped.

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
- Select a MIDI track to edit its notes in the piano roll. Click its bar ruler to move playback there, or drag it to scroll horizontally. Both rulers follow the same horizontal zoom and scroll position. Scrolling over the grid moves the visible pitch range.
- Audio tracks show the stereo WAV waveform. Open **Relay Circuit** and click bar 45 to hear the toms and dub chords together. In **First Light Vector**, click bar 49 to hear the gated chords and bell echoes. Solo each layer to hear its part.
- Studio starts with an untitled, empty 32-bar project in 4/4 at 120 BPM. Add the first part from the Track menu. **File → New project** opens another blank session. The transport sends pitched notes to assigned VST3 instruments. Choose an input with **IN**, arm the track with **R** and press the transport record button to capture notes from the current position. The navigation keys become available again when you stop recording. Stop turns the captured notes into a clip that can be saved. For audio takes, choose mono or stereo in the waveform editor.
- Use **File** to open a project or demo, **View** to switch editors, and **Settings → Audio / MIDI Devices** to select devices. The Windows x64 alpha supports **Windows Audio (WASAPI)** and **ASIO** with a compatible manufacturer driver installed. Apple silicon Macs use **Core Audio**.
- Arrangement rows have a horizontal volume fader, mute, solo, and an **Instrument** slot. **Mix** opens track-local volume, pan, output bus and return-send controls backed by the same mixer state. The row and Mix controls display the preset name when the plug-in reports it.
- The startup screen shows workspace preparation. **Plug-ins** opens a manufacturer-grouped VST3 browser. The first opening without a cache starts the scanner in a separate process and shows the current plug-in and scan progress. Subsequent openings use the saved local XML catalogue. Drag a plug-in onto an arrangement row to instantiate it without a new directory scan. **Scan VST3** checks for additions or changed files. Track slots display when an individual plug-in is loading.
- The Mixer puts voicing-role, vocal-role and pitched tracks on the left. Source chord voicings are shown as Global Timeline context when present. Use **Out** for Master or one of the project buses, **Send** for per-return send levels, and **+ Return** to add up to eight returns. Buses and post-fader returns sum to Master. The pre-fader **C** switch sends to Cue, using the third and fourth enabled output channels. Cue remains silent when fewer than four outputs are active. Tracks that share one instrument show its combined audio level on the track hosting the instrument.

### Level meters and track height

In alpha 3, each track header and mixer strip shows left and right sample peaks after its effects, pan and fader. The mixer also meters buses, returns, Master and the independent Cue output. The red indicator holds for one second when the signal reaches 0 dBFS.

At the bottom of the arrangement, choose a track type beside **Height** and drag the adjacent slider to resize the group. You can target all tracks, a section, or a type such as drums, buses or returns. **Mixed** means the rows currently have different heights. The **Zoom** slider controls the arrangement and piano roll together. Drag the divider between the two views to adjust their heights. Drag the grip beside the piano keys to resize the inspector and track headers together. Drag right to zoom in, double-click to fit the song, and use the adjacent scrollbar to move through it. Drag the lower edge of a track to change its height individually. Double-click the edge to fold the track or restore its previous height. Compact rows keep mute, solo, FX and metering within reach. Taller rows show more instrument and mix controls and display the notes inside their clips. Use the vertical scrollbar to reach the remaining tracks.

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

In the proposed design, Composer and each part arranger share chord analysis and progression generation. An arranger can use the chords accompanying an OSTF phrase, analyse unlabelled notes or suggest new harmony. The native editor lets the player try another ending, compare it on the same loop and keep the chosen take. [Arranger design (Japanese)](docs/ARRANGER_PHRASE_EXCHANGE.ja.md)

Choose **View → Theory Guide → Show Theory Guide** to see separate degree, function and progression lanes. T, S and D mark tonic, subdominant and dominant. An apostrophe marks a substitute. In C major, Dm7–D♭7–C appears as ii–subV–I. Every pitched track, including the voicing row, shows separate chord-tone and scale membership percentages weighted by sounding duration. Red marks under 50%, orange 50–74%, amber 75–89% and green 90–100%. A dash means there is no measurable material or reference. Drums and audio show “Not applicable”.

In the piano roll’s left panel, **Piano keys** shows a conventional keyboard. **Chord mode** names the current chords and colours each pitch by its interval from the chord root. **Scale mode** names the active scale and measures from its tonic. The lower stripe identifies the chord role, including perfect fifths, available tensions and avoid candidates. Over Cmaj7 with C Ionian, G is the fifth, D is an available ninth, and F is an avoid candidate when sustained against E. Explicit extensions take priority. Held notes change colour at harmonic boundaries while remaining one editable event. The colour key is below the editing controls.

Select notes to use **Quantize selection** with a grid and strength, or **Shape velocity** to draw a ramp from the first onset to the last. Notes beginning together receive the same velocity. Drag the body to move, either edge to trim, and hold Shift after starting the drag to temporarily disable snapping. Relative snap retains timing offsets. Absolute snap places the grabbed onset or end on a grid line. Both batch tools support Undo.

Select a drum track and choose **Slot steps** in the piano inspector. On a 1/16 grid, select the hi-hat row and use **Fill every 2** for eighth notes. Set **Hits per step** to 3 and click an empty step for a three-hit roll. Shift-click accents a new hit. Choose **At cursor**, a length of one, two or four bars, and **Loop** to audition the range. Switch to **Drum notes** to move individual mapped hits.

Open the **Velocity** selector next to the track name to edit CC, pitch bend, channel aftertouch or track pan automation. Click to add a point, drag to move it, or right-click to delete it. The Copper Mile lead has a bent guitar solo and moving pan from bar 33, followed by a 2/2 break at 104 BPM from bar 41. The preview supports meter changes on bar boundaries when the meters have equal bar lengths.

In the piano inspector, turn on **Pencil** to click in a note at the current grid size or drag right to draw its length. Press **B** in the roll to toggle the pencil. Alt/Option-drag paints a row of repeated hits, including on a triplet grid. Shift-drag selects a group. **Split into triplets** fits three retriggers inside each selected note, including drum hits. To play a chord as a strum, select its simultaneous notes, choose low to high, high to low or alternating strokes, set the spread as a percentage of a quarter note, and click **Strum chords**. One Undo restores the selection's original timing. Choose **Settings → Colour theme** to switch between Studio Blue (default) and Harmonic colours. The preference applies to the arrangement and piano roll across projects.

Double-click an empty cell to add a note. Drag its body to move it, either edge to trim it, or Alt/Option-drag to copy it. Shift-click or drag a selection box to edit several notes together. Ctrl/Command-drag vertically to adjust velocity. Choose a snap division in the piano-roll footer. Delete removes the selection, and Ctrl/Command+Z undoes the last note edit. The Help menu lists the shortcuts.

In Studio, drag **OSTF Studio → OSTF Arranger** from the Plug-in Browser to the regular Instrument slot. In an external DAW, load **OSTF Arranger** on an instrument track. In either host, open the Arranger editor and click **Choose instrument**. It lists VST3 instruments from the OSTF Studio scan cache by manufacturer. Use **Browse for VST3 file** for an unlisted instrument and **Edit instrument** to open the loaded instrument controls. The preview generates C–E–G–E from the host transport and returns stereo audio. The plug-in state stores the hosted instrument reference and settings. The hosted instrument must remain installed. The preview hosts one mono or stereo MIDI-capable VST3 instrument without audio inputs. External DAW interoperability remains to be tested.

Arrangement clips show the number of stored notes. In **Piano Roll**, the performance selector shows authored velocity and, when present, individual CCs, channel/polyphonic aftertouch and vocal pitch and expression curves by voice. **Paper Lantern Replies** has example CC11, channel pressure, pitch and dynamics on **Lead Vocal Guide**. CC, pitch bend and aftertouch can be edited in the lower lane. Vocal curves are read-only. Velocity drives pitched-note preview playback and MIDI export. An unspecified source velocity stays unspecified in the read model.

### OSTF and OSTFP files

**File → Open OSTF / OSTFP** opens supported OSTF 5.0 scores and projects in the same arrangement and piano-roll views. A plain score restores Studio settings when its Studio extension is present.

**Save as OSTF** writes the score and Studio mixer settings. If tracks have linked WAVs, it creates a sibling `<song>.media` folder and copies the WAVs there. The score references those copies by relative path. Move the score and media folder together. **Save as OSTFP** bundles the score, the Production Session 0.3 mixer document and the linked WAVs. Use **Instrument → Link WAV on this track…** to link one WAV per track for playback from the song start. The preview supports WAVs of up to 16 MiB each and up to eight linked files. Under **Output Tracks**, each bus, return and Master has its own arrangement lane with a fader and mute synchronized with the Mixer. Open **FX** on a source or output track to see up to eight stereo VST3 effects in processing order. Drag a row handle or use **Up** and **Down** to reorder it. Use the same panel to add, open or remove effects. Dropping an effect onto a track appends it. Source effects process instrument and WAV audio in order before the fader and post-fader sends. Output effects process the summed signal before the output fader. Changing a chain stops preview playback. Both save formats preserve these effect assignments and supported mixer and MIDI settings. Supported VST3 instrument assignments are saved as Production Session devices in OSTFP and in the common Studio production settings for standalone OSTF. Core MIDI Program Change uses the authored channel or the track channel. Instrument preset blobs and hardware settings remain outside this save implementation.

In the development build, use the toolbar to add melody and chorus parts, pitched, drum and audio tracks, buses and returns. Right-click a track name in the arrangement or mixer to duplicate or delete it. The magnifying-glass buttons change the interface size (75–150%) and remember it on this computer. Hover over an icon to see its action. Audio tracks have a **Link WAV** control. The preview supports up to fifteen source tracks. Drum-grid editing and recording on drum tracks are pending.

Windows Setup registers a document icon for `.ostf` and a package icon for `.ostfp`. If another default app is already set, select **Open with → Choose another app → OSTF Studio → Always**. The Portable ZIP does not register file types. The macOS app bundle declares matching document icons.

In alpha 3, the shared VST3 task in Windows Setup requests administrator approval to install both instruments and all nine effects in `%CommonProgramFiles%\VST3\OSTF Studio`. Other DAWs can find them with their system-folder scan. Setup keeps matching, versioned plug-in binaries in place and installs missing files. If a DAW is using a binary that needs updating, you can install Studio first. Save your session, close the DAW, and rerun Setup with the shared VST3 task selected to finish the update. After a successful shared install, recognized copies from older per-user installs move to `%LOCALAPPDATA%\OSTF Studio\Previous VST3`. The shared plug-ins have their own uninstaller. The Portable ZIP keeps them beside the app.

On macOS arm64, run **Install OSTF Studio and VST3.pkg** inside the DMG to install the plug-ins in `/Library/Audio/Plug-Ins/VST3`. Dragging just the app installs the application alone. Signing and notarization are pending.
- GitHub Actions Windows + macOS **preview** builds, unsigned
- `AGENTS.md` instructions for continuing in Codex
- Architecture/roadmap/checklists

Pending work includes full OSTFP conformance, SVP export, network sync, AI-assisted editing and signed binaries.

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

### Track inputs and patch changes

Track headers show mute in yellow, solo in blue and record arm in red. The headphone switch sends a pre-fader cue to output channels 3 and 4. On an audio track, IN selects an enabled mono input or adjacent stereo pair. Its input meter follows the recording gain even with playback stopped.

The Track menu offers initial and cursor-position MIDI patches. Select Program Change, CC 0 or CC 32 below the piano roll to edit changes over time. ChatGPT import analysis has Arrangement, Lyric and Vocal focus modes and a model-list refresh in the connected account menu.

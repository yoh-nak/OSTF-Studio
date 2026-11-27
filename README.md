# OSTF Studio

OSTF Studio is a desktop music workstation for arranging, playing and editing Open Song Timeline Format scores.

**[Illustrated user manual](docs/manual/en.md)** · **[Browse all plug-ins with screenshots](docs/plugins/en.md)** · [日本語マニュアル](docs/manual/README.md)

**Download Public Alpha 1** · [Windows x64 Setup](https://github.com/yoh-nak/OSTF-Studio/releases/download/v1.0.0-public-alpha.1/OSTF_Studio_Windows_x64_Setup.exe) · [Windows Portable ZIP](https://github.com/yoh-nak/OSTF-Studio/releases/download/v1.0.0-public-alpha.1/OSTF_Studio_Windows_x64_Portable.zip) · [Apple silicon DMG](https://github.com/yoh-nak/OSTF-Studio/releases/download/v1.0.0-public-alpha.1/OSTF_Studio_macOS_arm64.dmg) · [Release notes and corresponding source](https://github.com/yoh-nak/OSTF-Studio/releases/tag/v1.0.0-public-alpha.1)

![OSTF Studio Live Session showing the section queue, nine tracks and the vocal piano roll](docs/images/ostf-studio-live-session.png)

*Development preview · Paper Lantern Replies.* The session grid shows which takes will play in each section. The chord guide shows the current and upcoming chords, and the piano roll follows the selected track.

## OSTF 5.2 studio preview

Scale Editor pairs short labels with action icons. Hover over a toolbar button or use Tab to read its description, including which score or file it changes. Press Escape to dismiss the hint.

Bundled VST3 editors read Studio’s saved colour theme when their windows open. Their controls use the same console knobs and faders as Studio. Choose **Colour theme** in Studio, then reopen an editor to use the new finish. In Scale Editor’s instrument search, use the arrow keys to select a result, **Enter** to load it, or **Shift+Enter** to load it and hear C4 while keeping the list open.

Scale Editor shows **Display language** as three labelled choices in the header. The **Song style** field and its menu use the Studio theme’s text and selection colours. Both controls support Windows contrast themes.

Formant Singer recognises the small ッ before a matching English final stop in readings such as **Look / ルック**, **Let / レット** and **not / ノット**. It sustains the written vowel, including the オ in ノット, and reserves time for one consonant release. When the English word “hands” has the reading ハンズ, the voice sings the nasal and final /z/ without an extra pitched ウ. A separately written ド retains its stop. Literal Japanese readings keep their written sounds.

When a single lyric spans several pitches, Formant Singer sings the initial consonant once and releases the word on the last note. Its OSTF reader follows tempo changes inside a note when timing the consonants and vowels.

Choose an audition instrument and play a test note from the Scale Editor header, including before loading a song. **Read score** under **OSTF Studio** becomes available when the hosting Studio instance supplies its song. **Save as…** writes the plug-in document to a file. **Suggest scales** previews assignments for that document. In **OSTF text**, the song name identifies the open document. You can display its JSON in the text box, validate pasted text, or load the text box back into the plug-in. The keyboard retains separate preview MIDI output and MIDI thru switches. If a project loses a VST3 after moving a Portable folder, select the same instrument at its new location to recover its saved sound.

Formant Singer matches complete multisyllable English words against their written katakana, within one note or across syllable-aligned notes. In the Paper Lantern Replies chorus this lets the short ending of **little** keep the time and pitch of its own note. In Scale Editor, drag the header’s grip to the scale track in OSTF Studio with the same source score open. For a keyboard workflow, select **Copy scales**, then **Edit → Paste scales from Scale Editor** in Studio. Studio checks the source revision and provides Undo.

In Formant Singer, the lyric **street** with the reading **ストリート** sustains the written vowel and fits the surrounding consonants within the same note. This applies to one-syllable dictionary words whose pronunciation matches the supplied katakana. With **ストリート** entered in katakana alone, the singer keeps every syllable and gives **リー** more time on short notes. In Scale Editor, **Load and audition** plays C4 and keeps the instrument list open. Use **Load** after comparing instruments to close the list.

The arrangement keeps the **Scale** lane between Key and Meter even when the song has no assigned scale. Double-click an unassigned bar to choose a tonic and mode, or right-click and choose **Set scale for this bar** to override a song-wide scale. The assignment ends at the bar line or the next shared assignment. Undo restores the previous score.

Scale Editor keeps the current instrument and its preset when you audition it again. If an instrument fails to load, the picker stays open with the search and manufacturer filter intact. In Formant Singer, a short recorded vowel or sustained nasal now advances through its recorded timbre at up to the original speed. Long notes still use the full sustain. Incoming MIDI Expression (CC11) changes use a 4 ms ramp while a voice is sounding.

In Arrangement or Live Session, **Add take** can start a take group from several non-overlapping MIDI clips inside the selected section. Take 1 keeps the original notes, clip positions and intervening silence. The new take starts empty. Selecting a take for audition leaves the adopted comp in place until you adopt the alternative.

Select an audio clip to see its waveform and pitch lanes together. Double-click the waveform to add a warp marker, then drag it horizontally. Drag a note vertically to transpose its source interval in semitones. Arrow keys fine-tune the selected marker or note. New warps preserve pitch. Old rate-based warps keep their saved behavior. **Link aligned stems** shares warp edits with matching clips on other tracks and keeps their pitch edits independent. The editor reads pinned 5.2 note observations when present and estimates stable pitches in other WAVs in the background. Gray overlapping observations are available for inspection. Single-interval retuning processes all sound in that interval. Written percussion uses the supplied staff positions, and TAB preserves fingerings in the score, PDF and MusicXML.

Scale Editor includes explicit 5.2 vocal lanes when evaluating scale candidates and note fit, even when several voices share an instrument track. It counts the enabled comp output once. Formant Singer joins adjacent MIDI notes consistently when a host delivers note-on before note-off at the same sample and smooths velocity changes across connected syllables. A held voiced syllable can decay for up to 44 ms at note-off.

In **Scale Editor → Instruments**, select a manufacturer or search by name, select an instrument, then choose **Load and audition** to hear C4 through its audio outputs. The **Preview MIDI out** routing for comparison phrases stays available. **OSTF Formant Singer** now carries pitch into a connected sung nasal. With the Mei bank, a connected Japanese ん enters the voiced nasal directly.

New songs use OSTF 5.2. Studio opens 5.2 scores and Manifest 1.2 projects and preserves 5.1 MIDI takes and comp selections at their source version. In the arrangement, **Melody / Lyrics Tracks** groups the melody and vocal lanes at their original position. Declared lyrics, vocal and chorus names appear in the heading. A shared Core track sounds once. The plug-in browser and Scale Editor's instrument picker now have a manufacturer filter that combines with text search.

The score view, print path and MusicXML exporter read written parts from the 5.2 Notation profile. They support written pitches, separate voices and staves, ties and grace notes. MusicXML retains common enharmonic spellings and the initial transposition. Complex engraving and microtonal pitch export are incomplete.

The audio preview plays materialized Production regions, including selected comp regions with fades. Studio saves regional warp markers and pitch offsets in its Production host extension in OSTFP. Imported acoustic objects, render plans and source assets retain their identities. Other readers need support for the Studio extension to reproduce those edits. Authoring portable Audio Model edit plans, polyphonic note isolation and ARA hosting remain open. [Official specification and examples](https://github.com/yoh-nak/Open-Song-Timeline-Format-5.2)

## OSTF 5.1 studio preview

In **OSTF Scale Editor**, turn on **MIDI thru** to send played notes to another instrument through the host's MIDI routing. The receiving instrument gets the original channel, velocity and timing. **Preview MIDI out** separately routes the scale audition. On the shared output, a held keyboard note takes priority over an audition of the same pitch and channel. MIDI thru starts on for a new instance. Projects saved before this option was added start with it off.

**OSTF Formant Singer** places the release of a scored English /p/, /t/ or /k/ immediately before its vowel when using the Mei voice. This closes an audible gap in short syllables such as /ta/.

Scale Editor starts in the operating system's display language. The display language controls in the header offer **System**, **English** and **日本語**. System uses Japanese for Japanese locales and English for other locales. The choice is stored on this computer and also works when the VST3 runs in another DAW. Switching language keeps the selected chord and unfinished OSTF text. The native controls, instrument picker and main visual editing workflow support both languages. Some detailed theory explanations and diagnostics are still in English.

Formant Singer now carries the pitch between short recorded vowels across brief MIDI gate gaps and trims the lead-in when a short sung consonant follows another note. The regression phrase uses four 72 ms notes with 8 ms gaps and a final rest.

In **OSTF Scale Editor**, open a score with **Open OSTF** or drop it on the plug-in header. In the visual workspace, **Save** updates that file and **Save as…** chooses a destination. If another program edits or moves the file, the editor keeps the local edits available for Save As. A recalled DAW project asks for a destination on its first save. Enable **Preview MIDI out** to send an audition or **Test sound** on channel 1 to a downstream instrument in hosts that route VST3 MIDI output. The default audition plays the internal instrument.

**OSTF Formant Singer** connects adjoining recorded vowels with a short pitch transition. Adjust **Vibrato / scoop depth** to hear how much of the movement comes from the score. At 100% the instrument plays the written depth. At 40% it plays a gentler inflection, and at 0% it omits the two gestures. The host can automate the control and recall it with the instrument.

The eight gallery projects use Core 5.1.0 and include a shared-GM alternative. Open an OSTFP from File → Demo songs for its instrument patches, routing and insert settings. In the arrangement or live cell take menu, choose an authored comp to change the materialized performance. In Live Session, **Audition take** previews another comp independently. Queue the section to keep that combination, then choose **Adopt combination** to apply it to the arrangement. [Demo guide](examples/demo-songs/README.md)

Scale Editor opens the original Max device’s scale workspace inside the VST3. Drag the window border or lower-right corner to resize the workspace. Saving the project stores that size with the instrument. Play notes into the track to see them outlined on the candidate scale keyboard. **MIDI IN** names the held notes across octaves and channels. **Open…** and **Save as…** use the system file chooser. Scale and A/B previews use the audition instrument selected in the header or the configured MIDI output. Drop **Drag scales** onto the scale track of the same source song to return the current assignments. In an OSTF 5.0 or 5.1 score, choose **Harmony** for a shared scale or a pitched track under **WRITE TO** for its own improvisation scale. Choose **Preferred** or **Strict**, then **SET SCALE**. Studio checks the source revision and imports shared and track-specific assignments together. Undo in Studio restores both. **Apply scales** also commits the assignments to the plug-in score. Windows uses the Edge WebView2 runtime and macOS uses the system WebKit view. When an embedded view is unavailable, the editor offers its browser and file workflow.

Choose **OSTF Instrument Rack** as the track sound, then **+ Instrument layer** for each VST3 that should play the incoming notes. Choose **OSTF Effect Rack** in an FX insert to process the incoming sound in parallel. In either rack, select a layer to set its level or add serial effects with **+ Insert effect**. **Open plug-in** edits the selected device. Saving the project stores the layers and nested plug-in settings.

Arm a MIDI track with R, play, and use Capture MIDI on its mixer strip or input menu. Stopped performances start at the edit cursor. During playback notes use the arrangement position. The IN selector is shared by the mixer and track header. Drop an instrument or effect from the browser onto the channel strip. The output selectors on Master and Cue offer physical channels and device settings.

The transport, prompt guide and current vocal lyrics use black inset displays. Source track headers group the fader, pan and input controls above eight send slots. On narrow expanded headers the sends wrap when there is room for two rows. Each assigned return enables its corresponding send in the header and mixer.

**Paper Lantern Replies** displays its English lyrics in English and uses note-aligned katakana for the singer. Japanese verse text retains its original spelling.

**OSTF Scale Editor** is a VST3 instrument. Open it on a Studio instrument track, or use **Open OSTF** in another host. Select a chord, analysis lens and flow strategy to compare ranked scales. **Hear candidate** and **A/B with reference** play the original device's contextual phrases through a reference tone or a VST3 instrument. Click **Instruments...**, search by instrument or manufacturer, and double-click a result. The picker reads the Studio VST3 library and discovers instruments in portable and system bundles. **Test sound** sends C4 on MIDI channel 1 and works before opening a score. Choose **Reference tone** in the picker to check the host audio path. **Assign scale**, **Save OSTF**, and **Drag OSTF to Studio** apply, export and return the assignment.

Saving the project also saves work in progress in the embedded Scale Editor. Reopen it to resume on the selected chord and scale, with the pinned A reference, analysis settings and OSTF TEXT draft. Use **Apply scales** to commit the current assignments inside the plug-in. A text draft can be resumed even halfway through typing a JSON value.

Drag across the song scale map to select a chord range. **Include matching progressions** highlights its occurrences with the same chord sequence and timing. Assign applies the selected scale to the highlighted chords in a single edit. **Undo apply** restores the previous assignments.

The lead sheet and part score offer an A4 print preview with page navigation, adjustable margins and explicit page breaks. Enter the one-based bar numbers which should start a new page. PDF export shares the preview renderer. The master strip displays a stereo waveform below its phase scope and has its mute directly below its fader.

**Unmask** accepts an auxiliary VST3 audio input. In Studio, insert it on the signal to attenuate, open its FX chain, and choose the detector track under **Key**. This uses that track's pre-insert signal independently of its fader and mute. External hosts route audio to its Sidechain bus.

Japanese long-vowel marks within one unit extend the vowel recording. With English dictionary input, the Japanese bank uses a closed sound and a short release for B, D and G. Connected recorded vowels align their glottal pulses as the voice changes syllable.

The mixer pins the master on the far right. Independent white markers hold left and right peaks for 1.5 seconds. The headphone switch lights when Cue is active in the track header and mixer. Chord Voicing opens at 100 px and defaults to muted when the project has no saved voicing mix. Each source track has a pan knob and an independent named send knob for every return in both the expanded track header and the mixer. Buses and returns each support one to eight strips. Its two VU needles show the post-fader left and right signals, with a phase scope underneath. Source strips show one needle for mono and two for stereo. Bus, return and Cue meters follow their own signals. The master shows loudness, true peak, spectrum and phase alongside its stereo VUs. Click Instrument to open the instrument editor. Click FX to reopen the selected insert, or use its arrow to select another insert. A steady sine at −18 dBFS reads 0 VU.

During playback and seeking, each vocal part shows the current chord’s written lyric beside its lyric lanes. The pronunciation lane follows individual notes.

In the drum step sequencer, open **Loop range**, enter the first bar and length, then enable **Loop**. Page through the step grid while that range repeats, or choose **Use visible step range** to follow the grid again. The transport shows a coloured Camelot number beside a major or minor key. The built-in singer crossfades neighbouring syllables and times English vowel changes and consonants to the sung note.

Enable **Chord fit** or **Scale fit** in View to see a colour-coded percentage on each corresponding global timeline region. The denominator is the total pitched note duration sounding within that region across tracks. A targeted scale counts the notes to which it applies. Hover over short regions for a numeric reading.

The VST3 browser lists a scanned component class once across installation paths and prefers a bundled copy in a portable build. The fixed-phrase OSTF Arranger preview has been retired.

Editing note pitch or timing opens chord review for the affected intervals. Candidate percentages show how much of the selected track's sounding note duration fits each chord. The lane closes after the last decision. The toolbar and Edit menu provide Undo, Redo and Edit history for score and clip edits. Settings → Performance controls history entries, history memory and mixer-meter refresh rate. The CPU and RAM meters show system-wide usage. Hover over them for physical memory in use and total capacity.

In **Paper Lantern Replies**, bar 5 pairs the lead-sheet text 雨の音が with Am9. The lead singer articulates the kana on the melody notes. The backing track opens at 100 px. Its first written “Ah” fills the Dm9 chord in bar 18, and its pronunciation starts on the offbeat of beat two. Written lyric cells follow the chord boundaries, including chords without words. Use **First lyric** on either part to locate its entrance and **Overlay vocal parts** to view both melodies. Choose Mei or KAL16 in the singer editor. The selected voice sings both Japanese and English using its own recordings. English pronunciations come from the local dictionary. A note can specify a kana pronunciation while preserving the written English lyric.

Instrument and FX editors stay visible above the Studio arrangement while you work. Click **Edit** in an FX chain to open its controls directly. Click the arrangement to give it keyboard focus while keeping the editor in view.

The mixing collection adds 8-band, dynamic, graphic and parametric EQ, multimode and high/low-pass filters, gate, delay, de-esser, three compressor configurations and dedicated electric-guitar, acoustic-guitar and bass rigs. Each is a separate VST3.

Open **File → Demo songs → Paper Lantern Replies**, select **Lead Vocal**, click **First lyric**, then press **Play**. The lead enters at bar 5. The lead and backing singers receive their own notes, syllables and expression from Studio. The arrangement shows the lead's written lyrics and pronunciation. Select a vocal track to see its readings above the notes and its note-level pitch contours. Click a lyric cell or double-click a note to edit words, pronunciation, scoop or vibrato. The parameter lane also edits the depth of each technique.

**OSTF Formant Singer** analyzes the recorded vowel spectrum at load time. The note supplies the fundamental frequency and the sample supplies the evolving resonance envelope. Connected voiced syllables blend on the same pitch clock. The factory Japanese syllables come from offline HTS Voice Mei speech synthesis. English words sung with Mei use the isolated attack of a recorded mora for unvoiced consonants such as the final S in “lights”. Japanese syllables retain their full consonant and vowel. Choose **Load voice folder...** to use your own kana-named WAVs or a Japanese UTAU single-pitch CV folder with `oto.ini`. The editor displays the singer name and portrait if supplied in `character.txt`. Select **Mei** or **KAL16** to return to a built-in singer. In other VST3 hosts, open an `.ostf` in the instrument and choose a part to follow the host transport.

The first demo, **Neon Platforms**, plays eight tracks through two GM Sound Modules. At bar 17, open **GM #1** from Electric Guitar Hook and **GM #3** from Bass to compare their channel meters during the chorus. The number after # identifies the track hosting that instrument. Each part opens its own group's editor. **Sharing ▾** lets you disconnect the current part, or choose one or all parts from the host. Disconnecting preserves the tracks and clips.

Blue Hour Waltz (Shared GM) demonstrates seven tracks playing one module. In the arrangement, Chord Voicing appears above the lead. Each vocal part shows its display lyrics, pronunciation and melody in that order. Drag the dotted handle beside a backing vocal's name to move all three rows together. Arrangement tracks can also be reordered within their group. The track header menu offers one-step moves. Closing the mixer restores the track editor if it was open beforehand.

After editing pitched notes, review the chords directly above the piano keys at their timeline positions. Candidates use notes from all pitched score tracks, weighted by how long each note overlaps the interval. For example, the bass and piano can establish a chord under a single sustained melody note. Disabled clips and notes, drum triggers, and the voicing generated from the existing chord are excluded. Click a narrow interval for its choices or use the interval list. Resolve the affected intervals before opening another track. Inside each pitched clip, the upper-right corner of each chord interval shows its duration-weighted chord and scale fit. Hover over an interval for both named percentages at compact zoom levels. Both are enabled by default and have independent toolbar and View menu switches.

**English** · [日本語](README.ja.md)

**Media library** opens a folder of audio, MIDI and OSTF material. Drag a MIDI file into the arrangement to convert the selected part to editable OSTF notes and MIDI events. Drag an OSTF or OSTFP file to place a part, or use File → Open to restore its complete session.

A shared instrument belongs to its host track. Use the instrument menu to connect an existing MIDI track, add a new part on a free channel, or disconnect one or all parts. Track numbers distinguish two instances of the same plug-in. Disconnecting keeps the tracks and their clips. **Review unused MIDI parts** lists empty, unassigned parts with their generated names for optional removal. The GM editor meters all 16 parts. Selecting a preset in the editor offers to write the MIDI program to the destination track at the song start or edit cursor.

When several tracks share one instrument, open the GM Sound Module to see its sounding parts in **Playing Ch**. **EDIT CH** selects the part whose controls are shown. Open a child track's instrument menu and choose **Stop sharing this instrument**, or select the child from **Stop sharing with track** on the owner.

The **Audio files** toolbar button sits beside **Plug-ins**. Drop a sample onto the arrangement at the desired time. Drag the body of the resulting audio clip to move it or drag an edge to trim it. A track can hold several audio clips. On MIDI and audio tracks, drag the clip body to move it or right-click for **Split clip here**, **Join with next clip**, **Copy clip**, **Paste clip here**, and **Duplicate clip**. Click an empty time position to set the paste destination. With the arrangement focused, use Ctrl (Cmd on macOS) with C, V, D, E, or J for copy, paste, duplicate, split at the playhead, or join. Undo and redo use Ctrl/Cmd+Z and Ctrl/Cmd+Shift+Z. Clips can move to a matching track type. A placement that would overlap a clip is declined.

Insert **Stem Separator** on an audio track. Drop a file into the editor or use **Capture track**, play the passage, and select **Finish capture**. Drag the estimated Drums, Bass, Other and Vocals WAV files to audio tracks. Detected notes and chords are also available as standard MIDI files. Dropping the chord MIDI onto Studio's chord lane places the timed chord symbols. The local separator uses spectral and transient estimates, so stems can contain bleed. Results are stored under the user's Music folder in `OSTF Studio/Stems`.

In the upcoming release, mixer strips have metal-capped level faders and separate pan and send knobs with editable values. **Settings → Colour theme** offers seven finishes, including Forest Console, Midnight Indigo and Copper Console. The bundled GM Sound Module and nine stock effects use hardware-style controls. Each effect has its own rack panel and retains its existing automation parameters.

Choose AI Arrangement Studio, AI Lyric Studio or AI Vocal Studio from the **Mode** menu. The prompt guide beside the global timeline headings cycles through three example requests for the selected mode every 15 seconds. The previous and next buttons reset the reading time. The ChatGPT connection button sits at the right of the top toolbar, alongside the model selector and refresh button when signed in. Open **Settings → Mackie Control** to connect a control surface. Drag an analysed part onto a compatible track or into the arrangement to create one. Its notes extend the song as needed. Melody track bodies open at 100 px.

The **External connections** menu opens Splice, Melodyne and Ableton Link. Select an audio clip in the arrangement or the lower clip lane to open its source range in Melodyne. Opening Melodyne without a selection launches the standalone editor. Enable Link in both Live and Studio and enable Start Stop Sync in Live to share transport. The Link tempo menu shows the connected peer count. Audio capture holds its starting tempo for the duration of a take.

OSTF Studio is intended to become an AI-native DAW with OSTF 5.0 as canonical musical score, OSTFP as the project format, a powerful global musical timeline, VST3/AU hosting, optional audio assets, AI arrangement, and future distributed sessions.

## Sharing a multitimbral instrument

Load one instrument on a MIDI track. Open its instrument button and choose **Add MIDI track sharing this instrument**. Studio creates an empty part on the next unused channel. Existing tracks can choose **Instrument destination and channel** and see which tracks already use each channel. Use **Initial MIDI patch** or the program-change lane to change the part's sound. Audio returns in stereo on the instrument's track. **File → Demo songs → Blue Hour Waltz (Shared GM)** plays seven parts with one GM Sound Module.

## Mackie Control

Set the device to Mackie Control (MCU) mode and select its DAW MIDI input under **Settings → Mackie Control**. Select the feedback output to mirror faders, pan rings and button LEDs. The eight strips control volume, pan, record arm, solo, mute and selection. Bank and channel navigation move the strips across the mixer. Transport controls include rewind, forward, stop, play and record. Double-click a track name to edit it in place, press Enter to save or Escape to cancel.

Arm a MIDI track to play its assigned instrument while stopped and see its post-fader audio level. The track instrument menu's **MIDI OUT** destination sends live and sequenced MIDI to a hardware port. A separate audio input can receive the hardware's sound. Port selections are local to this computer.

Audio loops show a seam at each repeat. Editing a warp marker in one repetition updates the same point in every repetition. Extending a one-shot clip adds silence outside its source. To loop a MIDI phrase, right-click its clip to split at the pointer or cursor, enable **Loop clip**, then drag the right edge or choose **Add one repeat**. Studio writes repeated notes and MIDI events into the score and stores the period in a Studio extension.

## Recording in the lower editor (development build)

For a pitched or drum track, use **IN** to choose the MIDI input port and channel. Play a note to check the green input lamp. The audio output meter shows the level produced by the instrument. Press **R** on the destination track and then the transport record button. Only one track is armed at a time. The lower editor’s **Record** button also arms its track. Audio tracks display their waveform.

Open a pitched, drum or audio track. Click the editor ruler to park the cursor. Shift-drag on the piano or waveform ruler to select a cycle, or enable **Loop range** and drag. The step view uses its existing bar-range control. **Record** starts at the cursor. **Stop recording** finalizes the clip and closes the editor. Click the metronome face to toggle the click. Use its arrow to set the recording count-in to Off or 1–8 bars. The song-end button beside fast-forward locates the end of the project. MIDI passes overdub into one clip. Audio uses the first one or two enabled input channels and records a single WAV. On each pass, newly captured audio replaces that part of the current take. Replacing an existing clip requires confirmation. The per-clip WAV size limit is 16 MiB.

In slot steps the first pad appears at the bottom. Select a pad, assign its output note, and use **MIDI Learn** to bind the next incoming note to it. Enable the controller in Audio / MIDI Settings. A strike lights the pad and its row while stopped. **Move up/down** changes the kit order. The mapping stays on this computer. New views show up to ten bars, with 100-pixel arrangement lanes and compact global and output lanes.

## Import analysis (development build)

Use **File → Import and transcription** to analyse audio, MIDI or a PDF score. Review the musical parts in the result window, drag an individual part into the arrangement, or open the complete score in the current window. Long recordings can extend past bar 256. Audio analysis uses a provisional 120 BPM, 4/4 grid. The import limits of 60,000 notes and 15 source tracks still apply.

**Local analysis** runs offline. Local PDF recognition uses an installed copy of Audiveris. Use **File → Import and transcription** for partwise MusicXML (.musicxml, .xml or compressed .mxl), then open the result and choose **Save as OSTF**. The File menu also exports a multi-part MusicXML score or a ChordPro chord and lyric sheet. Compare the [eight-bar test PDF](examples/score-interchange/lantern-walk.pdf) with its [MusicXML source](examples/score-interchange/lantern-walk.musicxml). For **AI analysis**, use **Connect ChatGPT** in the global track header or **Settings → AI connections**. Studio uses the official Codex CLI and its ChatGPT browser sign-in. The first connection opens a dedicated Studio sign-in. If the browser does not appear, click **Open sign-in page**. The analysis selector lists connected providers. Choose the AI mode before selecting a file. Audio sends pitch, timing and timbre measurements, MIDI sends musical summaries, and PDF sends rendered score pages. Model estimates can be corrected after import. Disconnecting affects Studio's connection only. [ChatGPT sign-in](https://developers.openai.com/codex/auth) · [Audiveris CLI](https://audiveris.github.io/audiveris/_pages/guides/advanced/cli/)

## Audio clip editing (development build)

Select an audio track to open its waveform below the arrangement. Enable **Loop** and drag the clip’s amber right edge to repeat it. **Warp (re-pitch)** fits one cycle to the chosen number of quarter notes. Double-click the waveform to add or remove a marker and drag it to move that point in time. Pitch follows the change in speed. Choose **External connections → Open Splice Sounds** to open the scanned Splice Sounds VST3 in its own window. Studio accepts a downloaded WAV on an audio track. [Splice plugin and plan details](https://support.splice.com/en/articles/12997860-splice-sounds-plugin-faq)

Press **Melodyne** in the waveform editor to open the selected source as a working WAV in Melodyne standalone. Use **Replace Audio** in Melodyne, then **Apply edited WAV** in Studio to keep the edited audio in your project. The Theory Guide groups cadences directly on the degree row. The function row stays available underneath.

## Wave Analyzer (development build)

Insert **Wave Analyzer** on a track or master bus. It shows left and right waveforms, a 4096-sample spectrum, sample peak and RMS, stereo correlation, momentary/short-term/integrated LUFS and maximum 4× interpolated true peak. Hover over the spectrum for frequency and level. Freeze holds the display while measurement continues. Reset meters starts a fresh measurement on the next audio block. The plug-in runs locally and passes its input to the output with zero added latency.

## ☕ Support development on Buy Me a Coffee

OSTF Studio is in active development. If a demo makes you want to write a song with it, buying me a coffee helps me keep refining the GM instruments and testing playback on Windows and macOS.

**[Buy me a coffee](https://buymeacoffee.com/yohsukenak)**

## Opening older scores in alpha 3

Use **File → Open** to open a standalone OSTF 3.5.0, 4.0.0, 4.1.0, 4.2.0, 4.3.0 or 5.0.0 score directly. Opening an older song leaves the source file intact. **Save as OSTF** and **Save as OSTFP** write a 5.0.0 score and initially suggest a new filename. The preview supports up to fifteen source tracks.

## Transport and song information in alpha 3

The horizontal **Song defaults** strip beside or below the transport provides quick edits. **Details** opens the complete song information, credits and musical settings even in a narrow window. Select the **BPM** lane to open the tempo editor below the arrangement. Click to set a tempo or drag between two positions to draw an acceleration or slowdown. All colour themes use the same degree and note-underline palette. Local key and scale events apply in their own ranges. **Transpose or change tempo of score** shifts pitched notes and harmony and scales every tempo change. Use **Undo score edit** to revert. **Playback and display overrides** lets you audition another key or speed before committing to an edit. Opening a song resets the audition settings. Recorded audio plays at its native pitch and speed.

The Theory Guide toolbar button toggles the arrangement analysis. Arrangement tracks start at 100 px. Chord and scale badges share the clip header, so toggling them preserves track heights. Track headers distinguish pitched instruments, drums and audio with keyboard, drum and waveform icons. The piano roll shows chord names under the bar ruler. Select or drag a pitched note with harmony colouring enabled to read its chord and scale percentages. A sustained note is evaluated over the time it spends on each side of a harmony change.

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

Development builds of Windows Setup and the Apple silicon PKG let you uncheck individual VST3s or whole categories. On a fresh install all stock plug-ins are selected. Scale Editor appears under **Composition tools** and Wave Analyzer and Stem Separator under **Utilities**. To add a plug-in later, rerun the installer and select it. The Windows portable ZIP includes the full set.

### MIDI export, language and view controls

- Choose **File → Save as MIDI (SMF)** for a song-wide `.mid` file. Format 0 merges the parts into one track while retaining their MIDI channels. Format 1 writes a conductor track and named part tracks. Both preserve the source resolution, meter, tempo changes, notes, mapped GM percussion and supported channel events. Audio, plug-in state and vocal expression curves stay in the OSTF project. The derived Chord Voicing display does not create an extra MIDI part.
- **Settings → Language** offers the system language, English and Japanese. The selection takes effect on the next launch. An unsupported system language uses English.
- The icons below the menu bar open or hide the Piano Roll, Mixer, Plug-in Browser and Output Tracks. The lanes icon chooses which Global Timeline lanes to display. The View menu provides the same controls.

## This starter contains
- Portable C++20 timeline model with unit tests (internal only; not OSTF spec definitions)
- JUCE 9.0.5 desktop window with a **32-bar demo** ruler, global musical lanes, and named arrangement regions aligned to the same bar positions
- Standalone `.ostf` scores open in the workspace. The `.json` inspector accepts generic JSON objects and arrays.
- **OSTF 5.1.0 and OSTFP 1.1.0** support the score and mixer features described above. Studio also opens supported 5.0 projects. **File → Demo songs** opens the Studio gallery, including Copper Mile (Rock) and First Light Vector (Trance). **Open OSTF / OSTFP** opens supported files from disk.
- Select a MIDI track to edit its notes in the piano roll. Click its bar ruler to move playback there, or drag it to scroll horizontally. Both rulers follow the same horizontal zoom and scroll position. Scrolling over the grid moves the visible pitch range.
- Audio tracks show the stereo WAV waveform. Open **Relay Circuit** and click bar 45 to hear the toms and dub chords together. In **First Light Vector**, click bar 49 to hear the gated chords and bell echoes. Solo each layer to hear its part.
- Studio starts with an untitled, empty 32-bar project in 4/4 at 120 BPM. Add the first part from the Track menu. **File → New project** opens another blank session. The transport sends pitched notes to assigned VST3 instruments. Choose an input with **IN**, arm the track with **R** and press the transport record button to capture notes from the current position. The navigation keys become available again when you stop recording. Stop turns the captured notes into a clip that can be saved. For audio takes, choose mono or stereo in the waveform editor.
- **View → Live session** opens the section-by-track grid. Use **Visible tracks** to choose columns. The column headers provide mute, record arm, input, instrument, FX and track colour. Select a MIDI cell to edit its arrangement notes in the piano roll below. Scroll across the section boundary to see adjacent notes. A note that starts in the selected section can sustain into the next one. Audio cells show the names of overlapping regions. Switch the lower pane between **Piano Roll** and **Mixer** and drag the divider to adjust its height. **Tracks…** and **Sections…** provide add and duplicate commands. A new section adds four bars at the end. Duplicating a section appends a copy of its material.
- Each row shows its bar range and length. Cell colours show chord fit above scale fit. Hover for the exact percentage. Press **+** beside a section to put its selected combination and pass count in the performance queue. The same section can appear several times with different takes. Open **Queue** to change or remove a waiting entry. Start with the main transport. The next queued section starts after the current passes, and playback ends when the explicit queue finishes. Without a queue, playback follows the section list and skips zero-count sections.
- Both musicians can follow the current and upcoming chord above the matrix. **Performer display** enlarges these chords and shows upcoming section visits. Count-in works for live playback as well as recording. Turn the metronome off for a silent visual count-in.
- Click **T1** inside a MIDI cell to add or choose a take. Press the small triangle for independent local instrument or WAV audition. Toggle the circular arrow to repeat that cell. Choose **Adopt take** or **Adopt combination** to commit the choice to the arrangement. MIDI take ownership and adopted ranges survive saving to OSTF or OSTFP. The performance queue is temporary.
- Arm audio tracks with **R**, assign independent mono or stereo inputs, and record them together. One routed MIDI instrument can be armed alongside audio tracks. Each audio take receives its own WAV and uses the same recording clock.
- Live playback supports adding or duplicating a section at the end while the transport runs. The new row uses the shared song timeline. MIDI sources and audio regions belonging to the copied section are prepared before handover.
- **Take workflow.** The current Windows preview implements MIDI take selection and live section appends. The product specification also describes future audio takes, shared cuts and live structural editing. [Studio take specification (Japanese)](docs/TAKES_AND_LIVE_SESSION.ja.md)
- In **Score** and **Print score**, choose the instrumental part and lyric voice independently. Show written lyrics, pronunciation or both, and toggle chord symbols. Both print previews list the existing song credits so each line can be included or omitted. Songs without attribution display a disabled credit selector.
- **File → Print** opens dedicated lead-sheet and part-score previews. Paper size, orientation, margin, content and bar breaks apply to PDF export and printing. Opening pages include the credits entered in Song Information.
- Track colours link arrangement rows and mixer channels. Open the track menu to choose a colour. Mixer strips show the instrument, insert count and recording input activity.
- Use **File** to open a project or demo, **View** to switch editors, and **Settings → Audio / MIDI Devices** to select devices. The Windows x64 alpha supports **Windows Audio (WASAPI)** and **ASIO** with a compatible manufacturer driver installed. Apple silicon Macs use **Core Audio**.
- Arrangement rows have a horizontal volume fader, mute, solo, and an **Instrument** slot. **Mix** opens track-local volume, pan, output bus and return-send controls backed by the same mixer state. The row and Mix controls display the preset name when the plug-in reports it.
- The startup screen shows workspace preparation. **Plug-ins** opens a manufacturer-grouped VST3 browser. Search by plug-in name, manufacturer or category and combine the search with the instrument/effect filter. The first opening without a cache starts the scanner in a separate process and shows the current plug-in and scan progress. Subsequent openings use the saved local XML catalogue. Drag a plug-in onto an arrangement row to instantiate it without a new directory scan. **Scan VST3** checks for additions or changed files. Track slots display when an individual plug-in is loading.
- The Mixer puts voicing-role, vocal-role and pitched tracks on the left. Source chord voicings are shown as Global Timeline context when present. Use **Out** for Master or one of the project buses, **Send** for per-return send levels, and **+ Return** to add up to eight returns. Buses and post-fader returns sum to Master. Each headphone switch sends the track to the stereo Cue bus before its fader. The Cue strip meters the sum after its own fader, including on stereo devices. Enable four device outputs for a separate Cue feed on 3–4. To audition Cue through the main speakers or headphones, toggle **Listen on 1–2** on the Cue strip. Toggle it again to hear Master. Tracks that share one instrument show its combined audio level on the track hosting the instrument.

### Level meters and track height

In alpha 3, each track header and mixer strip shows left and right sample peaks after its effects, pan and fader. The mixer also meters buses, returns, Master and the independent Cue output. The red indicator holds for one second when the signal reaches 0 dBFS.

At the bottom of the arrangement, choose a track type beside **Height** and drag the adjacent slider to resize the group. You can target all tracks, a section, or a type such as drums, buses or returns. **Mixed** means the rows currently have different heights. The **Zoom** slider controls the arrangement and piano roll together. Drag the divider between the two views to adjust their heights. Drag the grip beside the piano keys to resize the inspector and track headers together. Drag right to zoom in, double-click to fit the song, and use the adjacent scrollbar to move through it. Drag the lower edge of a track to change its height individually. Double-click the edge to fold the track or restore its previous height. Compact rows keep mute, solo, FX and metering within reach. Taller rows show more instrument and mix controls and display the notes inside their clips. Use the vertical scrollbar to reach the remaining tracks.

### Global note tracks

OSTF Core stores chord voicing snapshots under `harmony.chords[].voicing` and performed notes under `tracks[].clips[].notes`. The **Chord Voicing** row displays note names and derives preview notes over each chord span. It has a mixer channel with its own instrument slot. Its channel settings are stored in the Studio extension without adding a Core source track. This derived row is the only global voicing row. A performed part such as **Keys Voicing** appears under **Arrangement Tracks**, even when its role is `voicing`. Tracks with the `melody`, `lead`, `vocal` or `vocals` role still appear above **Arrangement Tracks**. All these note rows open the piano roll and expose mute, solo, volume and pan.

### Factory synthesizers and audio samples

In **File → Demo songs → First Light Vector**, Prism Arpeggio plays the FM synth, Offbeat Sub the analog synth, Drums the drum machine, and Wide Halo Pad the wavetable synth. Each VST3 has four factory presets numbered PC 0–3. Select **Program Change** in a MIDI track's performance lane and use **Enter value…** to set the tick and program number. At bar 49 all four tracks switch to PC 2. Open the pad's Wave Analyzer insert to see the change in its spectrum.

Open **Plug-in Browser → Audio files** and choose a local sample folder. Drag a WAV, AIFF, FLAC or MP3 onto an audio track at the desired time. Studio offers to replace the clip on occupied tracks. Compressed audio and AIFF are decoded to PCM WAV for project playback. The current import limit is 16 MiB per asset, mono or stereo.

The information button opens song details. The adjacent note-and-arrows button opens the score and audition transpose/tempo commands. The performance lane's numeric entry accepts note velocity, program, controller, aftertouch, signed pitch bend and mixer pan values. Select one or more notes first when entering velocity.

### Shared instruments and GM Sound Module

Load **GM Sound Module** on one arrangement track. The module displays all 16 MIDI channels and their GM names, with channel 10 reserved for percussion. Select a channel to change its program, volume, pan, expression, tuning, pitch-bend range, sustain, mute or solo. Incoming Program Change and the supported MIDI controls update the panel. The module saves these part settings in its VST3 state.

Distribution builds use **FluidR3 GM** as the factory SoundFont. The bundle includes its MIT redistribution notice. The sample tables are shared by instances of the module. **Load SoundFont** still accepts a local SF2, and **Factory bank** returns to the distributed bank. For a development build with FluidR3, run `python packaging/prepare_fluid_r3.py` before configuring CMake. CI downloads the pinned Debian package and verifies its SHA-256. A build without the downloaded resources uses the original synthesized Studio Preview GM bank. [Debian package](https://packages.debian.org/sid/sound/fluid-soundfont-gm) · [Redistribution notice](https://sources.debian.org/src/fluid-soundfont/3.1-6/debian/copyright/)

On another track, choose **Instrument → Instrument destination** and select the track holding the module. Select the intended **MIDI channel** in that track's menu. Repeat for each part. Preview playback routes up to 16 Studio tracks to channels 1–16. The shared stereo audio, fader, mute, solo, bus and cue are controlled by the track holding the instrument. Per-part faders and individual audio outputs are pending. Track assignments are session-local.
- Tracks and mixer outputs scroll independently, with buses, returns, Master and Cue grouped on the right. **View → Global Timeline Lanes** controls the visible OSTF 5.0 lanes and saves that preference locally. Optional lanes show supported bar-aligned data when it exists in the score. Legacy demo fields are excluded from the menu.
- The VST3 browser can cancel a scan and retain saved entries. If a plug-in stalls for 25 seconds, scanning skips that plug-in and resumes, with a bounded retry count.
- Use **Instruments** or **Effects** in the plug-in browser to filter the manufacturer list. A keyboard icon marks instruments and a waveform icon marks effects. Drag a plug-in over an arrangement row and release when the row lights up to assign it to that track.
- If an installed VST3 is missing, choose **Scan one…** and select its `.vst3` file or bundle. This retries only the selected plug-in, including a previously skipped one. Windows commonly installs VST3 files under `C:\Program Files\Common Files\VST3`. Legacy VST2 DLLs are unsupported.


In OSTF Studio, open a supported `.ostfp` and drag the name of an arrangement track with pitched notes onto a MIDI track in another DAW. **Drag MIDI to another DAW**, at the right edge of the Piano Roll toolbar, does the same for the selected track. The Standard MIDI File keeps the notes' project-relative start, duration and pitch and uses a fixed velocity. The source project stays in OSTF format.

### Piano-roll performance lanes

Choose **View → Theory Guide → Show Theory Guide** to see separate degree, function and progression lanes. T, S and D mark tonic, subdominant and dominant. An apostrophe marks a substitute. In C major, Dm7–D♭7–C appears as ii–subV–I. Every pitched track, including the voicing row, shows separate chord-tone and scale membership percentages weighted by sounding duration. Red marks under 50%, orange 50–74%, amber 75–89% and green 90–100%. A dash means there is no measurable material or reference. Drums and audio show “Not applicable”.

In the piano roll’s left panel, **Piano keys** shows a conventional keyboard. **Chord mode** names the current chords and colours each pitch by its interval from the chord root. **Scale mode** names the active scale and measures from its tonic. The lower stripe identifies the chord role, including perfect fifths, available tensions and avoid candidates. Over Cmaj7 with C Ionian, G is the fifth, D is an available ninth, and F is an avoid candidate when sustained against E. Explicit extensions take priority. Held notes change colour at harmonic boundaries while remaining one editable event. The colour key is below the editing controls.

Select notes to use **Quantize selection** with a grid and strength, or **Shape velocity** to draw a ramp from the first onset to the last. Notes beginning together receive the same velocity. Drag the body to move, either edge to trim, and hold Shift after starting the drag to temporarily disable snapping. Relative snap retains timing offsets. Absolute snap places the grabbed onset or end on a grid line. Both batch tools support Undo.

Select a drum track and choose **Slot steps** in the piano inspector. On a 1/16 grid, select the hi-hat row and use **Fill every 2** for eighth notes. Set **Hits per step** to 3 and click an empty step for a three-hit roll. Shift-click accents a new hit. Choose **At cursor**, a length of one, two or four bars, and **Loop** to audition the range. Switch to **Drum notes** to move individual mapped hits.

Open the **Velocity** selector next to the track name to edit CC, pitch bend, channel aftertouch or track pan automation. Click to add a point, drag to move it, or right-click to delete it. The Copper Mile lead has a bent guitar solo and moving pan from bar 33, followed by a 2/2 break at 104 BPM from bar 41. The preview supports meter changes on bar boundaries when the meters have equal bar lengths.

In the piano inspector, turn on **Pencil** to click in a note at the current grid size or drag right to draw its length. Press **B** in the roll to toggle the pencil. Alt/Option-drag paints a row of repeated hits, including on a triplet grid. Shift-drag selects a group. **Split into triplets** fits three retriggers inside each selected note, including drum hits. To play a chord as a strum, select its simultaneous notes, choose low to high, high to low or alternating strokes, set the spread as a percentage of a quarter note, and click **Strum chords**. One Undo restores the selection's original timing. Choose **Settings → Colour theme** to switch between Studio Blue (default) and Harmonic colours. The preference applies to the arrangement and piano roll across projects.

Double-click an empty cell to add a note. Drag its body to move it, either edge to trim it, or Alt/Option-drag to copy it. Shift-click or drag a selection box to edit several notes together. Ctrl/Command-drag vertically to adjust velocity. Choose a snap division in the piano-roll footer. Delete removes the selection, and Ctrl/Command+Z undoes the last note edit. The Help menu lists the shortcuts.

The scissors and join tools in the toolbar act on the arrangement clip you click. Click the active tool again to return to selection. In a global lane, splitting preserves both values and joining combines adjacent intervals. Right-click the lane to choose which value survives. Section and meter cuts use the clicked tick. Cutting a meter interval keeps the existing downbeat.

After a pitched-note edit, each affected chord interval in the piano roll shows up to three ranked candidates and its own **Decide chord ▾** button. Select a candidate, then choose **Confirm** to apply it, **Keep** to retain the edited notes with the original chord, or **Cancel** to restore the affected notes. Cancelling a note that crosses a boundary restores the whole note. The left pane has separate **Confirm all**, **Keep all** and **Cancel all** buttons for the remaining intervals. Before an edit, candidates show a dash and batch actions are disabled. You can open the lead sheet, score or mixer during review and return to the same pending intervals. Resolve them before editing a different track. Dropping an OSTF or OSTFP part also shows its source chords against the score at the destination. Keeping the score chords retains the placed part.

Arrangement clips show the number of stored notes. In **Piano Roll**, the performance selector shows authored velocity and, when present, individual CCs, channel/polyphonic aftertouch and vocal pitch and expression curves by voice. **Paper Lantern Replies** has example CC11, channel pressure, pitch and dynamics on **Lead Vocal**. The lower lane edits CC, pitch bend, aftertouch, and per-note scoop and vibrato depth. Imported continuous vocal curves are read-only. Velocity drives pitched-note preview playback and MIDI export. An unspecified source velocity stays unspecified in the read model.

### OSTF and OSTFP files

**File → Open OSTF / OSTFP** opens supported OSTF 5.0 scores and projects in the same arrangement and piano-roll views. A plain score restores Studio settings when its Studio extension is present.

**Save as OSTF** writes the score and Studio mixer settings. If tracks have linked WAVs, it creates a sibling `<song>.media` folder and copies the WAVs there. The score references those copies by relative path. Move the score and media folder together. **Save as OSTFP** bundles the score, the Production Session 0.3 mixer document and the linked WAVs. Use **Instrument → Link WAV on this track…** to link a WAV for playback from the song start, or drop files at other positions on the arrangement. Studio supports up to 256 audio regions and WAV assets of up to 16 MiB each, subject to the total project size limit. Under **Output Tracks**, each bus, return and Master has its own arrangement lane with a fader and mute synchronized with the Mixer. Open **FX** on a source or output track to see up to eight stereo VST3 effects in processing order. Drag a row handle or use **Up** and **Down** to reorder it. Use the same panel to add, open or remove effects. Dropping an effect onto a track appends it. Source effects process instrument and WAV audio in order before the fader and post-fader sends. Output effects process the summed signal before the output fader. Changing a chain stops preview playback. Both save formats preserve these effect assignments and supported mixer and MIDI settings. Supported VST3 instrument assignments are saved as Production Session devices in OSTFP and in the common Studio production settings for standalone OSTF. Core MIDI Program Change uses the authored channel or the track channel. Instrument preset blobs and hardware settings remain outside this save implementation.

In the development build, use the toolbar to add melody and chorus parts, pitched, drum and audio tracks, buses and returns. Right-click a track name in the arrangement or mixer to duplicate or delete it. The magnifying-glass buttons change the interface size (75–150%) and remember it on this computer. Hover over an icon to see its action. Audio tracks have a **Link WAV** control. The preview supports up to fifteen source tracks. Drum-grid editing and recording on drum tracks are pending.

Recordable track headers show Mute, Solo, Record Arm and headphone Cue in that order. In the upper right of the mixer, **+ Bus** adds a destination to each track's output selector and **+ Return** adds a send destination.

Windows Setup registers a document icon for `.ostf` and a package icon for `.ostfp`. If another default app is already set, select **Open with → Choose another app → OSTF Studio → Always**. The Portable ZIP does not register file types. The macOS app bundle declares matching document icons.

In alpha 3, the shared VST3 task in Windows Setup requests administrator approval to install both instruments and all nine effects in `%CommonProgramFiles%\VST3\OSTF Studio`. Other DAWs can find them with their system-folder scan. Setup keeps matching, versioned plug-in binaries in place and installs missing files. If a DAW is using a binary that needs updating, you can install Studio first. Save your session, close the DAW, and rerun Setup with the shared VST3 task selected to finish the update. After a successful shared install, recognized copies from older per-user installs move to `%LOCALAPPDATA%\OSTF Studio\Previous VST3`. The shared plug-ins have their own uninstaller. The Portable ZIP keeps them beside the app.

On macOS arm64, run **Install OSTF Studio and VST3.pkg** inside the DMG to install the plug-ins in `/Library/Audio/Plug-Ins/VST3`. Dragging just the app installs the application alone. Signing and notarization are pending.
- GitHub Actions Windows + macOS **preview** builds, unsigned
- `AGENTS.md` instructions for continuing in Codex
- Architecture/roadmap/checklists

Pending work includes full OSTFP conformance, SVP export, network sync, AI-assisted editing and signed binaries.

## Stock effects and mixed demos in alpha 3

Insert **Harmonizer** on a track to mix its original audio with two pitch-shifted voices. **Voice A (st)** and **Voice B (st)** set fixed intervals from −12 to +12 semitones. **A / B blend %** selects the balance between the voices (0% is A only), and **Wet %** mixes the harmony with the original. The default intervals are a perfect fifth and an octave above the input.

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

## Official formats and gallery

The gallery contains seven songs in eight projects. Package loading verifies declared scores, hashes and supported production documents. The current bar-based editing grid supports meter changes that preserve bar length. Packaged WAVs are supported. [Demo arrangements](examples/demo-songs/README.md) · [Official 5.1 release](https://github.com/yoh-nak/Open-Song-Timeline-Format-5.1/tree/957bd071cecc58a12f417ad212c40723ba9812ca) · [Fixture provenance](tests/fixtures/README.md)

## Licensing
OSTF Studio's original application and bundled plug-in code is available under **GPL-3.0-only**. Distribution uses the GPLv3 option for the ASIO SDK and AGPLv3 for JUCE. Each release includes the matching source and build scripts. [GPLv3](LICENSE) · [Dependency licenses and source](licenses/NOTICE.md)

### Track inputs and patch changes

Track headers show mute in yellow, solo in blue and record arm in red. The headphone switch sends pre-fader audio to the Cue strip. On an audio track, IN selects an enabled mono input or adjacent stereo pair. Its input meter follows the recording gain even with playback stopped.

The Track menu offers initial and cursor-position MIDI patches. Select Program Change, CC 0 or CC 32 below the piano roll to edit changes over time. ChatGPT import analysis has Arrangement, Lyric and Vocal focus modes and a model-list refresh in the connected account menu.

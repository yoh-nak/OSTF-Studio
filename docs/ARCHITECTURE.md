# OSTF Studio — architecture proposal (not normative OSTF spec)

The [illustrated manual](manual/en.md#track-tools) distinguishes the 41 VST3s in Public Alpha 2 from the 46 in the Windows development build. In that build, Studio hosts scope-compatible editing tools alongside the track instrument. Note edits can target an individual take. Motif Expander can capture a phrase, audition a variation and write it to an alternate take while preserving the adopted performance. The [plug-in guide](plugins/en.md) identifies the five development additions.

The [current OSTF specification and registry](https://github.com/yoh-nak/Open-Song-Timeline-Format) govern format work. Review its default branch before changing a reader or writer. Studio embeds the reviewed schemas for the file versions it implements. The upstream status reviewed at [e207a09](https://github.com/yoh-nak/Open-Song-Timeline-Format/blob/e207a097bfee7c57e53c5fb86216b75b0cea1595/STATUS.json) identifies 5.4.0 as the source baseline and 6.0.0 as draft design.

Global boundary edits operate in source-score ticks. The arrangement reads shared edge positions from the score when it builds the preview. Moving one shared edge resizes both neighboring spans. Removing a meter change keeps MIDI ticks and audio placement while changing the meter map. Its section anchors index musical bar numbers separately from the linear tick axis. A later authored meter change keeps its tick even if this shortens the preceding measure. [Build targets and cache policy](BUILDING.md)


The [user manual](manual/en.md) covers standalone Studio and the hosted workstation. The [illustrated device inventory](plugins/en.md) covers all 41 VST3 editors in the Windows build of Public Alpha 2. Public Alpha 1 shipped 38. The subsequent three are the workstation VST3, SUPER KNOBBER LEAD and SUPER KNOBBER REVERB.

The workstation plug-in and standalone app share arrangement, notation and piano-roll editing. With host transport follow enabled, the workstation maps the host time position through the score tempo map. The embedded Scale Editor reads the active Studio project and writes shared or track-specific scale assignments there. The [timeline guide](manual/en.md#timeline-edit) describes the boundary rules, and the [export guide](manual/en.md#audio-export) shows how a track or clip selection becomes a separate audio track.

## Offline arrangement rendering

Global boundary edits run as one score transaction. Adjacent duration events share the edited edge, while key and meter use change points. Shared scale edits leave track-targeted assignments intact. The drum ruler uses the piano-roll viewport. Scale Editor audition cues use the audition audio sample counter, so its readout follows rendered audio through changes and rests.

Bundled instruments and effects with exposed automation parameters use instance-local MIDI CC learning. Bindings persist by stable parameter ID and MIDI channel/controller in the plug-in state. Track inserts receive controller messages along with their audio input.

Standalone Studio and the hosted workspace prepare a private PreviewTransport on the message thread. Each enabled instrument and insert gets its own instance and a snapshot of the live plug-in state. The worker renders stereo blocks from tick zero at the selected sample rate. Mix export follows arrangement playback, including adopted takes, audio edits, buses, sends, master inserts and the selected tail. File replacement follows successful encoding. Hardware MIDI instruments require an audio recording of their output.

Single-track bounce renders the instrument, inserts and detector inputs before the strip fader. A group bounce sums the selected sources with their strip levels and pan. Selection uses track and clip IDs separately from piano roll focus. Excerpt renders filter source notes and regions by clip ID and seek a private transport to the first selected tick. The new muted audio track keeps the excerpt at its original position. WAV regions split at score ticks to fit the OSTFP media limits. Imported Productions that require the original track layout support mix export.

The part score can share the workspace with the editable piano roll. Both use the same part and score edit history. The audio clip editor places timing above detected pitch and writes clip-local semitone changes independently of linked warp markers.

## SUPER KNOBBER REVERB

The independent stereo effect `Skr1` uses the supplied Reverb v1.0 GenExpr delay network, the connected Max preset tables and 108 direct controls. The two shimmer pitches use Signalsmith Stretch. The resizable editor uses Studio's console finish and displays measured input, wet and pre-EQ spectra, band decay settings and wet level history.

## Assigned scale audition

Scale Editor's project workspace builds read-only audition notes from harmony scale spans or the selected track's improvisation directions. It clips spans to the selected section or song and converts ticks through the tempo map. Distinct scales assigned simultaneously leave a rest. The instrument rack schedules the preview against its audio sample clock and releases notes on stop or project replacement. The native controls are a fallback when the system web view is unavailable.

## SUPER KNOBBER LEAD

The independent instrument `Skl1` ports the supplied LEAD v1.2 voice, drive and wet-effects GenExpr to C++. Its three voices share the source patch’s audio routing. The dry stereo sum goes directly to the output and a parallel drive stage feeds chorus, flanger, phaser, delay and reverb. The optional 2x voice engine decimates through a 65-tap Blackman-windowed low-pass before the effects. Both modes report 16 samples of latency. The factory bank contains 188 programs in 37 genres. Stable VST3 parameter IDs expose the musical controls directly. DAW state stores the selected program, edited controls, MPE and quality per instance.

The Windows host test scans the built bundle, checks the bank, automation and recall, renders a timed phrase at 44.1 and 48 kHz, and measures folded energy on a sustained saturated saw. The independent Max reference fixture uses the original device’s Gen~ processors with a sample-clocked performance input. MIDI dispatch inside Ableton Live and macOS host operation require separate verification.

## Studio as a hosted instrument

`OSTFStudioPlugin_VST3` shares the standalone `MainComponent` and `PreviewTransport`. Each processor owns its workspace for the lifetime of the instance. An editor window can detach while playback continues. A virtual stereo output feeds Studio's existing audio callbacks from the host's process block. Host MIDI events retain their sample offsets. The hosted device manager bypasses hardware setup. The instrument catalog excludes Studio itself to prevent recursive loading.

The optional transport follower observes play, stop and position discontinuities on the audio thread. A message-thread timer applies transport changes and interprets elapsed seconds using the score tempo map. Start and seek have message-thread latency. Sample-accurate starts, host tempo automation, multi-output routing and offline bounce remain future work.

DAW state contains a private versioned wrapper around an OSTFP session with packaged assets, nested instrument/effect state and the follow preference. These bytes are specific to the plug-in and add no fields to the portable OSTF format. The standalone executable provides new plug-in scanning and PDF import for the hosted workspace.

## OSTF 5.2 score and audio projection

Studio and the bundled plug-in editors resolve the stored `workspaceTheme` against the same console finishes. Each plug-in window owns its JUCE look-and-feel and reads the view preference when constructed. The Scale Editor embeds colours from that finish in its HTML and gives the detached instrument picker its own look-and-feel. Theme selection stays in local view preferences.

The console look-and-feel supplies foreground and background pairs for native combo boxes and popup menus, including their highlighted row. Scale Editor passes the same text and selection colours to its HTML controls. Native form controls use the dark colour scheme. In forced-colours mode the browser uses the system text, canvas and highlight colours.

The Singer’s English reading matcher recognises a katakana geminate before a matching syllable-final stop. The dictionary stop represents closure and release together. An explicitly written オ can carry the dictionary AA vowel of “not” or “clock”; the complete reading must still match. The written ンズ in a matched English final /ndz/ uses the nasal and /z/ without a separate stop or pitched support vowel, while ンドズ retains /d/. The existing duration planner reserves consonant time and assigns the rest to the sung vowel. If the complete written reading fails the match, the literal phones are retained.

The Scale Editor VST3 embeds the M4L analysis and audition workspace. The left chord pane reads assignments for the selected harmony or instrument target. Unassigned chords display suggestions. A native editor supplies scale controls when a WebView is unavailable.

For a lyric unit aligned to several pitches, the Singer orders referenced notes by absolute score tick. It carries the last vowel nucleus across pitches and schedules the offglide and final consonants on the last pitch. The Singer's OSTF reader integrates the shared tempo map between note endpoints, allocates articulation in seconds and maps inner edges back to host PPQ.

Scale Editor keeps the missing child's plug-in description and opaque state in the host session. When the user reselects the same manufacturer and VST3 class, both the picker and file browser pass the stored state to the new instance. Choosing another instrument starts it with its own settings. The workspace header opens the same picker as the native controls.

For a multisyllable English word, both vocal playback paths partition the pronunciation at its vowel nuclei. Where a word spans several lyric units, each syllable must fit its corresponding unit, including any explicitly written reading. The mapping is committed after all units match. On a single note, primary stress and shortened diphthong offglides affect relative time. Separate notes keep their authored boundaries.

The Studio instance supplies an in-process, message-thread VST3 callback bound to the current project generation. Each edit carries the SHA-256 digest of the source revision. Studio merges shared harmony scales and explicit track improvisation directions into its own score and records one history entry. It sends the resulting revision to connected editors on edit and undo. An external DAW hosts the workstation VST3, which provides the same connection to its own child instruments.

Studio transport and the standalone Formant Singer score reader share the lyric-unit pronunciation pass. A complete match between an English monosyllable and its katakana reading lets the scheduler use timed consonants for the supporting kana, keep the written Japanese vowel and shorten the offglide of a diphthong. A partial match falls back to the entire supplied reading. For katakana-only units containing one marked long vowel and only short supporting kana around it, the scheduler gradually shortens those supporting syllables below 115 ms per mora. All syllables retain their Japanese phonemes. The mapping takes place during score preparation and preserves note endpoints and authored lyric data.

Scale Editor keeps the filtered instrument picker open during repeated auditions. The test note goes to the hosted instrument even when preview MIDI is routed to another track. A successful explicit Load closes the picker.

The arrangement always projects a Scale lane. A song-level scale fills gaps between timed regions, and a score with no scale assignments displays an empty lane. Adding a shared scale from the lane intersects the clicked bar with the gap between other shared assignments. Targeted scales retain their own scope. The edit and Undo use the same score transaction as the other global lanes.

The Scale Editor instrument picker compares JUCE plug-in identities before replacing the hosted instance. Re-audition of the active instance preserves its live state and native editor. Formant Singer keeps a four-millisecond MIDI Expression ramp per channel during active voices and applies Expression immediately when a channel is silent.

Recorded CV syllables can shorten the consonant opening to leave room for a pitched vowel. Once the stable vowel begins, the spectral envelope advances at up to the recording's natural speed. This also applies to the sustained body of a recorded nasal. The pitch clock and note duration still come from the score.

The waveform and pitch lanes use the same source-to-timeline mapping. Each note belongs to the trimmed source interval, so moving a warp marker also moves its note. Pinned observations are matched by asset ID and SHA-256. A cancellable worker estimates stable fundamentals for recordings without matching note objects. Overlapping observations are read-only. Retuning acts on the entire selected interval in both channels. Linked warp edits require matching start, region and source duration, and existing timing settings.

Playback preparation uses Signalsmith Stretch to render pitch offsets and pitch-preserving warp against the tempo map before the audio callback. The callback reads the prepared buffer for transport, seeking and consolidation. Production linear fit supports both standard pitch policies. Studio stores non-linear warp and source-relative pitch offsets in its host extension. Saving a timing edit to a supported imported Production updates the companion digest and retains source assets, analyses and comp references.

Scale Editor records the selected audition destination alongside each test sequence. The picker can audition the nested instrument locally with external phrase routing enabled. The audio callback flushes previously sounding notes to the previous destination. Singer uses the existing voice pitch clock to connect a new voiced nasal and enters the recorded nasal sustain during legato. Detached nasal attacks and unvoiced stop closures retain their own timing.

Score 5.2, Manifest 1.2 and Production 0.5 resolve schema references from embedded official resources. Schema validation covers structure. Studio projects supported musical features independently of the lossless source document. The special-track display follows voice references to Core track IDs. Written Notation parts take priority over MIDI-derived engraving. Visual positions use rounded tick endpoints, while the OSTF retains rational notation and source attachments. MIDI take selection uses the same native comp operations for 5.1 and 5.2.

Production-only audio channels have local playback rows. Materialized regions determine what sounds. Analysis hypotheses and render dependencies remain in the package. Saving a previously imported rich Production preserves its members and updates the score binding for supported score edits. Studio detects unsupported changes to its imported mixer or audio regions before writing. External assets need separate handling. Distributed and plug-in companion documents can be retained as bytes. Applying or rewriting their transactions is outside this path. Unsupported detector routings and third-party state-asset restoration are reported on open. Official packages 04 and 06 exercise those cases.

The offline pitch study measures a stable monophonic fundamental, transposes the user's selected interval using Signalsmith Stretch, compensates algorithm latency and crossfades both edges into a new WAV. A future editor can keep note candidates and voiced/unvoiced intervals in the Audio Model, audition a pitch edit, render a separate derived asset, and bind a materialized region to it. Detection on expressive vocals, ARA hosting and polyphonic source separation each need separate validation.

### Take editing, observations and written percussion

For a new MIDI take group in a 5.1 or 5.2 score, Studio sorts the contained ordinary clips by onset. It writes a separate disabled source and a materialized output for each clip, with a comp region for each intervening silent interval. Editing a mapped output preserves the other outputs' note links. Existing linked performances, overlapping clips and partial source intervals still need an authored comp map.

The waveform editor indexes note observations from the 5.2 Audio Model by source asset ID and SHA-256. The selected region supplies the frame range. Hover uses the clip timing map to resolve the original decoded frame, including phase, looping and warp markers. The UI reads stored estimates and preserves alternative observations. Interactive pitch-edit plan authoring remains on the roadmap. The notation projection retains unpitched instrument IDs, written staff positions and normal or cross noteheads. MusicXML instrument references are scoped to the written part.

Scale Editor resolves special vocal lanes through the lyrics voice's Core track ID in both adviser runtimes. Candidate analysis excludes silent probabilities, drum triggers and disabled takes. Singer groups same-sample MIDI events on the audio thread and releases outgoing pitches before allocating incoming voices on their channel. Same-key event order and positive-duration overlaps retain their MIDI semantics. A short velocity ramp carries connected notes to the authored velocity. Voiced releases scale with the held duration up to 44 ms. Short notes and unvoiced endings retain the earlier 28 ms envelope.

## OSTF 5.1 score and production session

Scale Editor copies the host MIDI input before sending it to its nested instrument. The outgoing event bus merges that input with routed audition events at their sample offsets. Live keys have priority when both sources use the same note on the same channel. The merger tracks keyboard notes and pedals per channel so disabling thru releases them on the next audio block. The VST3 instance stores the thru switch. New instances enable it, and restoring state without the switch preserves the original local-only keyboard routing.

When the English phoneme renderer uses a Japanese voice bank, it measures the attack before the recorded vowel. A scored unvoiced stop uses the last part of its allocated duration for this release, leaving the closure at the beginning. A shorter allocation compresses the attack. Live MIDI without a duration starts the attack immediately. Vowel-only banks use a short noise release when no recorded stop is available.

Scale Editor uses Studio's display-language resolver and keeps its own per-user preference under the OSTF Studio application settings directory. An absent preference selects the OS display language. Choosing a language updates the JUCE controls and sends the resolved locale to the embedded workspace. The resource provider also supplies the locale in the initial HTML. Labels from the original UI template are registered before loading a score, and dynamic controls localize their own labels. Song data retains its original content.

Formant Singer treats up to 12 ms between voiced vowels as a connected pitch change when the incoming phone has a scored duration of at most 150 ms. Other durations retain the 3 ms join window. A scored scoop takes precedence. A smooth release reaches silence within 28 ms. A short connected Japanese consonant-vowel sample begins at its detected attack before the pitched sustain. The regression covers the rendered onset pitch, settled pitch, nasal onset level and final silence.

An optional VST3 event output carries scheduled audition notes on channel 1. Host input still reaches the nested instrument. Changing routes cancels the phrase and releases outstanding notes through their original destination. Formant Singer smooths the nominal pitch of connected recorded vowels for up to 22 ms, bounded by one quarter of the incoming phone duration. Its automatable pitch expression depth scales the authored scoop and vibrato before synthesis. A 15 ms ramp smooths control changes during playback.

Studio validates Core 5.1.0, Production Session 0.4.0 and Manifest 1.1.0 with the pinned upstream schemas. Existing 5.0 projects retain their format. Playback reads enabled materialized MIDI clips. Whole-section take creation keeps alternate sources disabled and writes a complete region map and note links. Choosing a comp for adoption updates the output flags and activeCompId together. Audition creates an isolated MIDI projection. Each live cue stores the selected comp ID per intersecting group when queued. Queued selections can be adopted together in one undoable score edit. Whole-take note edits update their source. Studio retains complex source/region maps when it cannot safely author them. Visit-based comp scheduling, audio comp editing and distributed playback are open work.

Production device automation restores authored normalized parameters at tick zero on stock inserts. Instrument patches in the demos use score program changes. Recorded vowels supply short-time harmonic magnitudes measured over neighbouring pitch periods. A preallocated inverse FFT rebuilds band-limited cycles from the source envelope at the score harmonics every 5 ms. Adjacent voiced phones share a continuous pitch clock. At a voiced legato join the incoming cycle is circularly aligned to the outgoing waveform once; the same offset is used on subsequent frames of that phone. The Mei English B/D/G path uses a closed voiced interval and a bounded consonant release. Unvoiced onsets retain their time-domain playback path.

The Scale Editor instrument picker uses the Studio VST3 catalogue and reads class metadata from installed bundles. It instantiates the selected instrument on the message thread. The track audio callback processes its output and sends audition notes on MIDI channel 1. For short Japanese recorded morae with a known duration and a marked vowel, the Singer removes leading silence first and budgets up to half the note for the consonant before switching to the sung pitch.

Scale Editor presets store audition routing, the selected instrument, window dimensions and view settings. The Studio project owns the musical document. On recall the editor displays the score supplied by Studio. Existing private scores and text drafts from older builds pass through preset saves unchanged so they can be recovered using the older build. New instances store only settings.

For English M, N and NG sung with a Japanese bank, the renderer sustains the recorded syllabic nasal. M and N can use the initial closed portion of the matching consonant-vowel recording for articulation. Banks without a syllabic nasal use a recorded closed attack when available, or a synthesized nasal resonance. The Japanese lyric alignment also carries a sustained nasal across notes with a length mark.

For a recorded Japanese consonant-vowel syllable, voicing detection identifies the end of the raw attack. The renderer traverses the voiced opening into the vowel at its recorded rate and stretches the remaining vowel to the note duration. Very short notes compress the opening into at most half the available voiced time. Live MIDI input follows the same opening and then approaches the end of the recorded vowel gradually.

## Section performance and presentation

A performance cue captures a section, pass count and source-track take ranges. Pending cues have distinct identities even if the section repeats. Queued MIDI and chase events are prepared on the message thread. The renderer switches them at the section boundary. The shared chord guide reads the same visit order as the transport. Studio keeps the queue in memory and uses the existing Studio take extension for 5.0 projects and native MIDI take groups for 5.1 projects.

Cell audition uses a separate transport and isolated local plug-in instances. Each audition reads one source track over its section, with its own loop and position. Armed audio inputs share a recording clock and capture buffer. Each selected physical channel range is written to a separate region with its track's gain.

The section-input matrix projects a single editable slot per section and source MIDI track from the existing score. Existing clips keep their identities. The matrix and arrangement share the same piano roll, editing actions and undo history. Slots select source ticks independently of launching a whole section or adjusting its remaining passes. The harmony display groups analysis cells by chord event within each section, summing matched and total note durations for that track before calculating percentages. Undefined fits stay neutral. The grid uses inclusive displayed bar numbers and the actual length of each finite section span.

The first note entered into an empty slot creates a score clip spanning the section. If existing clips occupy part of the section, new clips fit the free interval around the note. Later edits use the clip already covering that tick.

The live track chooser filters grid columns by document and source track IDs. Mute and arm use the shared mixer and recording state. The adjustable lower pane hosts the arrangement piano roll or mixer. Appending a section stages score material with fresh IDs and records its audio-region edits in the same undo entry. Print preview, PDF and the native print path use the selected credit lines to lay out the first page.

Live playback projects OSTF section IDs and source tick ranges into a finite local performance order. Repeat counts belong to the current performance. They can change until a boundary is consumed. The transport splits hardware blocks at those boundaries and uses precomputed MIDI chase buffers. Note edits are validated on the message thread and published at a boundary. A bounded look-ahead returns ordered section visits in score ticks, including repeats. A network or physical performer still needs an adapter for clock deadlines, performer identity and proposal arbitration.

Arming an audio track in Live routes its selected mono or stereo input through the track inserts, mix, sends and outputs. Recording input gain is shared with the mixer. Portable track colour overrides use OSTF presentation object colours. Printer configuration stays local. Native printing and the PDF preview use the same page layout and opening credit allocation.

Mixer and arrangement strips share the input-route and plug-in assignment handlers. Master and Cue resolve physical output pairs against the active device mask before passing packed buffer indices to the renderer. The selections are local to the device. Retrospective note input uses a bounded 4,096-event buffer and the most recent 120 seconds of performance. MIDI callback work stores fixed-size events. Clip creation, score validation and undo run on the message thread.

The Studio sidechain source is tapped before track FX. Instruments and regions are rendered before inserts so even a later track can feed an earlier detector in the same block. The stock Unmask uses a stereo-linked five-band detector. A supported production-session sidechain connection names the source channel and Unmask auxiliary port, with a pre-fader tap and independent mute. Print preview and PDF export share A4 page boundaries and page painting.

A directly instantiated Scale Editor without a Studio project connection displays instructions for loading the workstation first. Project callbacks check the owning Studio lifetime, current generation and revision before accepting scale changes.

The Scale Editor instrument hosts a mono or stereo VST3 instrument, receives host MIDI and renders scheduled phrase previews in the audio callback. With its editor open, Studio can run the instrument in its stopped-transport monitor path. The selected instrument description and opaque state belong to the plug-in session. An unassigned slot uses a reference tone. The instrument embeds the supplied Max device's read-only analysis, flow, phrase audition, lens and genre JavaScript modules. JUCE runs these on the editor thread. OSTF writes use the native schema-validated document path. The native scale map supports range selection and matching progressions, retaining one validated score revision per batch. It matches exact chord identity, relative onsets and duration. The embedded workspace writes explicit track scale directions with preferred or strict adherence. Non-scale directions remain owned by the source score. The reduced native editor offers chord-targeted harmony edits.

Arrangement source headers show eight stable send positions. Active positions address the corresponding mixer return. Unassigned positions are dimmed and disabled. Expanded headers adapt the send grid to their width and row height.

File-picker, desktop-open and bundled demo requests share a background reader. The message thread owns the progress overlay, adopts the parsed song and restores its session. Each request has a sequence number. A late result cannot replace a newer song. Parsing failure retains the open session.

The singer stores the selected voice bank in the plug-in state. Its offline phoneme mapping adapts Japanese morae or English dictionary phonemes to that bank before synthesis. Individual meter channels hold their latest peak for 1.5 seconds and then release at 18 dB per second.

Lyric headers look up the written chord cell of each displayed part at the transport tick. Playback invalidates the lyric header when that cell changes, including when seeking backwards. Pronunciation stays note-aligned.

The mixer master has a right-pinned viewport. The mixer and arrangement send controls address the same return index. Removing a return compacts every send array. The final bus and return are protected and each group has eight slots. Stereo VUs use independent envelopes. The master instruments share one continuous surface. On wide panels the scope and VUs sit beside the analysis readout. Smaller mixer windows provide horizontal scrolling. The readout uses loudness, true peak and correlation from the post-master signal and computes its spectrum on the UI thread. Two calibrated, average-responding VU envelopes and a chronological ring of stereo samples are measured after master processing and gain, before the Cue monitor switch. The drum editor keeps its optional, bar-addressed playback/recording loop separate from the visible editing page. Both use source-score ticks.

The local singing instrument analyses pitch marks when a voice bank loads. Sustained grains overlap at the output pitch while retaining the source waveform inside each grain. Adjacent syllables overlap briefly using the outgoing renderer. Studio supplies phoneme duration in its private articulation message. The instrument also accepts earlier messages and derives durations from host tempo in imported-score mode.

The footer and File menu expose the score and clip edit journal, including the state of pending chord decisions. Selecting an earlier or later entry uses the same restoration path as Undo and Redo. The platform meter samples system CPU counters and physical memory once a second on the message thread.

Formant Singer plays the recorded consonant onset at its source rate, then transitions to the pitched vowel. A voiced continuation on the same channel keeps a single oscillator phase while the harmonic magnitudes change to the next phone. Sustained notes gain a delayed, low-depth pitch variation within the note's authored expression.

The bank loader detects periodic voicing across the nominal consonant boundary and records the first audible attack sample. For English unvoiced consonants sung with a Japanese CV bank, the renderer uses samples up to that boundary and tapers the release. A short noise model supplies missing attacks. A Japanese CV crosses the same boundary into the tuned vowel.

## Shared instruments, track ordering and voice processing

Instrument Rack and Effect Rack are VST3s with an internal JUCE processor graph. Each layer has a serial chain and a gain stage feeding the shared stereo output. Instrument layers receive the same timestamped MIDI. Effect layers receive the same input audio. The plug-in state contains the ordered device descriptions and their parameter-state bytes. A missing device preserves its state and silences its layer. The host can reopen a moved stock device from its portable or installed VST3 folder.

The offline workspace sends confirmed assignments on the message thread. Instrument and effect windows forward the hosted editor’s resize constraints to the decorated window.

Scale Editor records its size in VST3 state. The audio callback tracks held input keys by MIDI channel before the audition scheduler adds its notes. A message-thread timer forwards changes in the twelve pitch classes to the browser. The diagram keeps the scale and chord annotations visible inside the outlined keys.

The scanner retains file paths for incremental scans. The browser and instrument/FX menus share a catalogue keyed by VST3 component identity and manufacturer. It prefers an existing portable bundle and omits the retired OSTF Arranger preview. During theory analysis, each pitched part caches matching and total note ticks for each global chord and scale event. Combining those ticks yields the ensemble percentage. The derived chord voicing, percussion, audio and disabled events do not enter the ensemble denominator.

Studio creates native editor windows when the user opens an instrument or effect. JUCE associates the window with the Studio workspace so the OS keeps it above its owner. Editing an FX slot dismisses the chain popup before showing the editor. Background audio-device and control-surface setup can initialise without creating a desktop window.

OSTF Harmonizer is a mono/stereo audio insert. Two Signalsmith Stretch processors transpose the same input by separate fixed chromatic intervals. The processor delays the dry signal by the analysis and synthesis latency, reports that latency to the host, and smooths changes to both mix controls. The VST3 state stores both intervals, voice balance and wet level. It uses the existing effect catalog and session state mechanism.

The session stores display order as a list of stable source track IDs in its Studio host extension. Sorting occurs within the fixed timeline groups. A reorder therefore leaves MIDI destinations, audio routing, automation and source clips attached to their original tracks. Opening a shared part resolves its instrument owner before looking up an editor. The owner alone renders the merged channel-specific MIDI streams.

The drag handle and the header menu use the same placement rules. Generated chord voicing precedes the lead and backing vocals. Each voice owns two lanes above its note row. The first uses timed phrases from its lyric documents and the second uses readings aligned to its notes. Backing vocals move as whole groups. Display and pronunciation visibility can be switched independently. The mixer temporarily occupies the track editor's pane and closing it restores the earlier layout.

Each shared member opens the owner's editor from its instrument button, with routing on the adjacent arrow. Loading the current instrument from the browser or file picker reuses the shared instance. Replacing it from a member requires confirmation and targets the owner.

The mixing processors share parameter/state infrastructure and expose separate catalog identities for automation and project recall. Studio and the formant singer's VST3 file player share a note-to-lyric reader. It prefers a lyric unit’s pronunciation, groups kana into morae and reads note-targeted scoop and vibrato. Studio sends articulation and note events through its plugin host. In file mode the VST3 embeds the score in plugin state and follows one selected melodic line at the host's musical position.

The singer builds a lookup table for a user-selected WAV or UTAU CV folder outside the audio callback. It estimates each recording's fundamental period, preserves its attack and loops the vowel to sustain a note. If a consonant recording is missing, the singer can combine a synthesized onset with a recorded vowel. Plugin state stores the local folder path. A missing folder falls back to the embedded Japanese Mei sample bank. The portable musical score uses the existing OSTF 5.0 lyric and performance fields.

The arrangement projects the lead vocal’s lyric units onto note spans. The piano roll uses the selected part’s note IDs for words, pronunciation and delivery. Editing a word updates its official lyric unit and editing a technique updates its note target. Shared or multi-unit lyrics are protected from ambiguous single-note rewrites. The renderer and editor use the same scoop and vibrato pitch functions. A lyric-bearing track with an unassigned instrument can restore the bundled Singer when opening a score, including a standalone OSTF without saved mixer state. The Studio-hosted VST3 always accepts live track events. Other hosts can also use the plugin's score file player.

## Arrangement tools and chord decisions

Chord review occupies space while decisions are pending. Ensemble notes rank the candidates. Each candidate reports the duration-weighted fit for the selected track during that interval. Each pending interval has a decision menu with Confirm, Keep and Cancel. Its button follows the interval when zooming or scrolling. The fixed gutter has a separate button for each batch action. Each pending interval records the original score events for its edited note IDs. Cancellation restores those events and removes inserted notes. Overlapping intervals share the rollback of a sustained or moved note. Keep preserves the edited performance and existing harmony. A candidate menu remains available for narrow intervals. The host preserves pending intervals across mixer and reading views and requires resolution before switching the edited track. Fit analysis accumulates note overlap in ticks per clip and context, including applicable note- or clip-scoped scales. Both fit readouts have independent display preferences.

Chord ranking uses the score's pitched source tracks together. The analysis indexes enabled notes by start tick and prefix maximum end tick, so a sustained note contributes on both sides of a boundary. Every note edit or material drop builds the index from the current destination score. Session mute/solo is an audition choice and does not change the ranking. The generated chord-voicing lane is excluded to avoid feeding the existing chord back into its own analysis.

The split and join toolbar tools dispatch to the same MIDI and audio commands as the arrangement context menu. Global spans and change points have distinct boundary rules. A split retains both sides of the value. A join uses the selected earlier or later value over the combined interval. The score validator checks references before a change is committed.

Pitched-note gestures collect the old and new sounding ranges and rank chord templates using the selected track's duration-weighted pitch classes, missing tones and bass. The UI presents candidates in rank order. The user can keep the existing harmony even if the edited notes suggest a different chord. For OSTF material, Studio translates source times through both tempo maps and also offers each source chord including its authored voicing. Individual and bulk decisions replace only the approved intervals. The decision list is transient UI state and participates in local score undo/redo.

OSTF 5.0 performance plans can visit a source section again and specify a fixed, open or silent realization for each participating track on each visit. Phrase instances already have a repeat count. The MusicXML exporter currently writes the linear score. Folding equivalent measures into repeat barlines and representing variant endings are future notation-export work. Compare the actual parts, rhythm, ties, articulations and dynamics before folding. A performer or composer can retain intentional variation without changing the source timeline.

## Shared instruments, sample placement and stem exports

A routing group is identified by the owner's score track ID. Each member stores its destination and MIDI channel in Studio production data. OSTF 5.0 and OSTFP can restore multiple instances with the same plug-in identifier. Each GM meter measures samples from that part's SoundFont voices before their stereo outputs are summed. The bundled instrument editors notify Studio about manual program selections using per-part edit gestures. Score playback and state restoration do not emit those gestures. A third-party instrument exposing an indexed VST program list can propose a track update while stopped. A confirmed edit writes a program event and keeps existing bank selects.

The media browser passes audio, SMF, OSTF and OSTFP to the arrangement's file drop handler. Score material uses the import placement path. Opening a project separately restores the whole production session.

The shared instrument owner receives channel-remapped events from its routed parts. Its stereo return is mixed once. The GM editor reads cached bank metadata and publishes channel activity from MIDI onsets and sounding synth voices. Each editor part selector edits a single channel while all channels can play. Detaching a child removes its route and reactivates its prior instrument assignment if available.

The folder toolbar button and the right pane use the same sample browser. File drops resolve to arrangement time. Moving a clip changes its start tick and keeps its source and warp data. Each production audio region has a stable ID, track, source range, independent timing and duration. Same-track clips share a mixer and can reference one WAV. The transport decodes equivalent source ranges once. The arrangement hit-tests a region by time and uses the same edit commands for drag, menu and keyboard input. Audio splits retain warp phase, loop period and padding. Joining matching slices restores the source region. Joining independent sources renders a new bounded WAV, including silence between clips. MIDI copies remap owned IDs and joins retain clip-relative event offsets. The score undo history also records audio region revisions. Dropping a file over a single existing audio clip offers replacement. The Stem Separator VST3 passes its input through, records bounded mono or stereo host audio on request, and analyses file or captured input on a background worker. Studio's importer and the VST3 share the local complementary stem estimates and note/drum/chord analysis. Each completed result is a directory of WAV and standard MIDI files. The chord MIDI carries playable triads and timed text markers under a named track so that Studio can also place it on its chord lane.

## Factory instruments and the sample browser

Four separately identifiable VST3 instruments share a polyphonic rendering engine. FM uses phase modulation, analog uses band-limited saw oscillators, the drum machine models pitched and noise transients, and wavetable synthesis interpolates harmonic-limited tables. The processor takes MIDI events at sample offsets and captures the active preset per voice. Its VST3 state holds preset, tone and output level. First Light Vector stores program changes in the score and instrument class references in the production session.

The right pane uses the JUCE directory browser for local audio. Drag payloads capture the selected file path, and internal and operating-system drops share track placement, replacement confirmation and asset limits. Performance numeric entry uses the same score edit, undo and playback paths as drawn points.

## Score interchange

The File menu exports Synthesizer V project version 113. The adapter writes 705,600,000 blicks per quarter note, converts the start and end of each enabled arrangement note, and carries the tempo and meter maps. Voice-specific lyric alignments and performance note IDs separate singers sharing an OSTF 5.2 core track. Overlapping notes use additional monophonic tracks. One lyric unit sustained across notes uses a continuation, and an equal number of units and notes is paired in musical order. Untimed units retain authored order. Wordless humming uses `m` and an unlabelled pitched note uses `la`. The transfer covers the score and lyric text. Custom phones, expression curves, plug-in audio and voice libraries are outside this mapping. [Dreamtonics track model](https://sv2.docs.dreamtonics.com/en/voice-setup) · [SVP file model](https://github.com/SoulMelody/LibreSVIP/blob/master/libresvip/plugins/svp/model.py)

MusicXML partwise documents and compressed MXL containers feed the same in-memory score import and validated OSTF save path. Local PDF recognition passes the Audiveris MXL output directly to this reader. Full-song MusicXML exports the source notation parts, with harmony on the first part. ChordPro exports chord symbols with aligned lyrics. These are score interchange views of the current project.

## Mixer and plug-in controls

Studio reads and writes the Production Session device fields `enabled` and `bypass`. A bypassed instrument contributes no instrument audio. A bypassed insert passes its input on to the next slot. Real-time playback and independently auditioned cells share the state of each device. For slot types represented only in Studio's existing host extension, the switch is stored alongside the local chain. Parallel racks retain their individual device switches in the rack's plug-in state.

The host scopes its console LookAndFeel to the main component. Workspace preferences store a stable finish key, while musical degree colours and status indicators retain their semantic palettes. Mixer pan, send and level controls use the same channel callbacks as the track headers. The GM instrument and stock effect editors own their own LookAndFeel instances so they render consistently in other VST3 hosts. The effect editors attach each control directly to its existing ranged parameter for gestures, host automation and state recall.

## MIDI control surfaces

Mackie Control reserves a user-selected OS MIDI port. Channel-strip and transport messages enter a bounded queue and the UI thread applies them to the current eight-track bank. Musical MIDI routes ignore the reserved port. An optional output sends changed fader positions, pan ring values and button states. Both port identifiers live in local preferences.

Record arm reuses the transport's live MIDI filter, destination and render chain. Stopped monitoring processes the armed destination while the score cursor stays parked. Hardware destinations dispatch timestamped MIDI, bypass track audio processing, and release held voices on stop. The MIDI input lamp and post-fader audio meters report different signals.

## Media analysis and external editors

Source tracks have an exclusive record arm. Each MIDI track filters incoming channel messages by its selected port and channel before updating the input lamp. A recording snapshots the input filter for monitoring and capture. Port selections are stored locally by document and track ID.

The Mode menu selects the arrangement, lyric or vocal guidance used for AI analysis and remembers it in local Studio preferences. A read-only prompt guide beside the global timeline headings cycles through three local examples for that mode at 15-second intervals and restarts the interval on manual navigation. The upper toolbar holds the ChatGPT connection control and, when connected, the model selector and catalog refresh action.

The import window snapshots the selected provider and model for each worker job. The model selector offers automatic selection and the visible models in the Studio CLI profile’s catalog. An explicit selection is passed to the CLI for analysis. Local code decodes source events. For cloud analysis an AnalysisProvider adapter performs account checks and schema-constrained musical requests. The ChatGPT adapter delegates sign-in to Codex CLI. Studio starts the official browser flow in a private CLI profile and checks that profile for ChatGPT authentication. The sign-in button can reopen the pending authorization URL. Disconnect signs out that profile. A local preference stores Studio opt-in. Long dropped parts extend a temporary score through the last converted event and only replace the live score after successful placement. Audio note candidates are assigned to parts in batches, then MIDI or audio pitch profiles are analysed for harmony. Native Windows and macOS PDF rendering supplies page images for AI. Local PDF recognition reads the MXL produced by Audiveris from a temporary copy. Both routes validate the resulting in-memory score before offering track placement or document replacement. Audio FFT windows are read in bounded overlapping pages. Windows PDF analysis renders one page per worker process with Windows.Data.Pdf and passes the PNG back to the parent before starting the next page.

The arrangement lower pane binds to the selected global interval. Edits use the same validated score transaction and undo history as the arrangement. New shared scales occupy the available part of the selected chord, or the clicked bar when no chord is present. Chord-scoped edits split a spanning scale and extend authored harmonyScales references to its pieces. Scale Editor auditions use its local audition instrument. If the chosen range has no assignments, it previews ranked chord scales without editing the score. Conflicting assignments are reported; gaps in partially assigned passages remain rests. A stopped, unarmed preview can reach the master independently of its track mix.

## Editor recording

The device sample clock drives the editor cycle and one-bar count-in. The count-in holds the score cursor and excludes input from the take. MIDI note-offs are paired on an elapsed sample counter and split into score-time intervals across wraps. Audio copies the first enabled inputs into a bounded buffer allocated before the device callback starts. A new pass replaces covered samples in that buffer. Stop encodes 16-bit PCM on the message thread and applies the resulting region. Clip warp and source timing belong to the region. There is one audio region per track in this preview.

Notes, velocities and ordered kit membership use Core 5.0. WAV assets and regions use the Production profile. The existing Studio host extension stores the editor recording cycle and per-track capture width. The local device preferences hold the MIDI input-to-output note map. The user can share a project with different hardware.

## Import results and editing

The import panel holds a draft in memory. Opening the draft loads it in the original Studio window. Dragging a result converts its source ticks through seconds into destination ticks at the drop position. Chord changes use the same schema-validated edit transaction as the global timeline editor. The backing audio files remain available for later playback and explicit project saves.

## Overall product distinction

The song settings editor writes the authored key and scale to the song and edits the initial tempo and meter in the canonical timeline. Whole-score transposition and tempo scaling build a candidate score and validate it before replacing the active model. The score edit history also holds these operations. Audition transposition and tempo scaling belong to the open transport. MIDI playback and the transport display use the temporary values, while file and MIDI export use the authored score. WAV playback keeps its native sample rate.

The piano chord strip shares the note grid's time axis. During a pitch or timing drag the editor analyses the selected track's prospective notes on the UI thread. The selected-note percentages weight its intersections with harmonic intervals by duration. Arrangement clip headers aggregate the sounding durations by clip ID and chord interval. Scale changes inside an interval use the scale applicable to each portion of the note. Each visible interval has its own pair of fit badges. Committing or cancelling a drag discards the temporary analysis.

- OSTF = portable musical structure (score and musical intent), independently portable.
- OSTFP = container for song + optional production/session/assets, default save project.
- DAW runtime = audio device, hosting, transport, timeline, editors, clipboard, AI integration.
- OS-local settings = hardware device IDs, auth, user-specific settings, plugins scanning paths.

Toolbar and track-header actions share the score editing, mixer routing and plug-in hosting paths. The mixer's **+ Bus** and **+ Return** buttons use the same output-editing command and availability checks as the main toolbar. The mixer supports eight buses and eight returns with stable saved output slots. Removing a bus redirects its tracks to Master. Removing a return clears its sends. Interface scale is stored in the local view preferences.

## Project lifecycle

A new session reads the embedded empty OSTF template through the same reader as a standalone score. The six playable Studio demos use the OSTFP reader. The upstream comprehensive example remains a parser regression fixture. Launching a document from the operating system opens that document directly in the workspace.

## Independent modules

The proposed Composer and part arranger VST3s each embed the same host-independent C++ library for chord analysis and progression generation. Each instance keeps its own working progression. Existing OSTF chords are imported with the selected phrase, including their timing, bass and voicing. Gaps can be analysed from notes or filled with a musical suggestion. The user auditions and accepts the result before generating a part. Placing the chosen progression on Studio’s global chord lane lets multiple arrangers use it through read-only OSTF Plugin Context Exchange snapshots. External hosts can work with OSTF files, MIDI files or captured MIDI. The fixed-phrase preview plug-in has been retired. [Arranger workflows and acceptance criteria (Japanese)](ARRANGER_PHRASE_EXCHANGE.ja.md)

The planned native editor aligns chords and notes on one beat ruler. A part can regenerate a selected range using the surrounding phrase as context. The processor owns the accepted and alternate performances across editor lifetimes and project saves. Analysis and generation run outside audio processing. Playback switches prepared takes at a loop boundary, and both export paths read the accepted notes.

The planned Studio chord comparison keeps the current score visible alongside a proposed progression. The default acceptance changes chords alone. Optional relative assignments and generation dependencies identify parts for review. Users can audition and adopt selected part changes together with the progression as one undoable edit. Opening a score plays the saved notes.

A separate optional arrangement overlay estimates how the sounding notes fit each chord. It can evaluate hand-played clips without generation history. It marks intervals with enough pitched evidence, gives a reason on selection, and groups audition targets at song-wide zoom. The design caches results per interval and recalculates affected ranges off the audio thread.

Selecting a chord highlights its span across clips. The inspector reports the share of overlapping pitched note duration in the chord and in each applicable OSTF scale. Held notes contribute only their overlap on either side of a boundary. Scale targets restrict which notes and spans enter the denominator. Missing or overlapping scale contexts remain visible.

The arrangement and piano roll share a derived Theory Guide. The arrangement groups recognised progressions with brackets on the degree row and shows harmonic functions on a second row. The same-key, contiguous chord matcher includes plagal and deceptive resolutions and I–vi–ii–V–I or iii–vi–ii–V–I turnarounds. A dominant seventh a semitone above an adjacent tonic can stand in for V. A diatonic IV or iv can occupy the predominant position. An adjacent diatonic target in the same key can identify an applied dominant seventh. A preceding minor or diminished-fifth ii relative to that target extends the reading to a local ii–V. All pitched parts display separate duration-weighted chord and scale statistics at every track height. Percussion and audio have an unscored indicator. The chord denominator spans the chord even when a scale changes inside it. Scoped scales use section, chord, track, clip or note identities. An explicit improvisation scale is available as a local reference. An ambiguous overlap can be inspected one reference at a time. Missing context and silence have no percentage. Chord and scale views label the active reference. Degree fills use the chord root or scale tonic. A separate stripe identifies the chord role. Authored compound extensions take precedence over the sustained minor-ninth avoid heuristic. Its hit testing and edits still operate on whole source notes. The UI builds the interval cache when a guide is enabled and updates a part after note edits. Playback reads cached results and repaints the cursor strips until the viewport scrolls. A/B comparison and acceptance of an arranger proposal remain planned.

Drum slot editing retains the source slot ID alongside the MIDI mapping. The step view uses the existing note transaction and computes ratchet boundaries from the ends of each grid cell. Named slots can share a MIDI key. A slot without a mapping remains editable and requires a mapping for MIDI output. Clock-based loop playback splits a device block at the selected range boundary. The message thread prepares the controller and held-note state for a seek or loop restart. While a drum loop plays, slot edits stage a new MIDI timeline. A buffer swap at the next loop boundary keeps the device sample clock and existing plug-in instances running.

MIDI performance points retain their clip-local event ID and optional channel. The editor presents absolute song ticks, then resolves the destination clip on commit. Production channel pan uses a separate step or linear envelope keyed by source track ID, resolved to the saved production channel on export. It applies to rendered audio before routing and sends. The inspector reads the same harmonic role colours as the note underline, and selection is drawn over the note fill.

The preview renderer publishes stereo sample peaks from the processed source audio, output stems and a dedicated Cue bus. The bus sums post-insert, post-pan, pre-fader sends and is metered after the Cue gain on every device. The third and fourth enabled output channels receive this buffer as a pair. The mixer can switch the primary monitor pair between Master and Cue, independently of the Master meter and offline export. The audio thread accumulates peaks in fixed-size lock-free mailboxes. The UI consumes each peak once per refresh and shares it with the mixer and arrangement. A shared instrument's combined audio is measured on the track that owns it. Track heights and vertical scrolling are view state scoped to the open song. Group height changes use the score track kind, global note role or output destination. New tracks inherit their group height. The bar ruler stays fixed. A single horizontal range and scrollbar drive the arrangement and piano roll, including both rulers, note editing, the playhead and the velocity lane. The inspector plus piano keys occupy the same width as the track headers. A draggable divider controls the vertical split. Quantize, velocity ramps, pencil input, triplet subdivision and chord strumming use the transactional note-edit and undo paths. Pencil drags commit on release. Repeated strokes skip occupied onsets at the painted pitch. Each fractional grid boundary rounds against the project origin so triplets stay in time at non-divisible PPQs. Subdivision clones two events before shortening the original, retaining its ID and assigning new IDs to the copies. Each selected chord uses one total strum spread in quarter-note time. Equal pitches share an onset, and the spread is capped by the shortest member and the next selected onset. Original release times are preserved. The local workspace theme controls arrangement clips, harmonic row shading, note fills and the inspector legend. The score and MIDI export are independent of this preference.

Wave Analyzer uses the same catalog and a dedicated VST3 component/editor. A mono or stereo insert reads host samples and meters 100 ms hops with continuous K-weighting, 400 ms overlapping loudness blocks, a 3 s window, a bounded 0.1 LU relative-gate histogram and a four-phase true-peak FIR. A nonblocking snapshot transfers the latest waveform and readings to the editor. The message thread calculates calibrated per-channel Hann spectra and draws stereo phase. UI refresh, freeze and editor lifetime are independent of the measurement clock.

The nine stock audio effects use separate VST3 component classes and share a JUCE DSP library. The demo generator, packaging checks and Studio's VST3 identity mapping use the same effect catalog. The Production Session describes owned instruments and stock effects in processing order, bus destinations, gains, pan and post-fader sends. The Studio host extension carries edited stock effect state and accommodates legacy insert lists. The synthetic chord-voicing row keeps its settings in the extension. The offline audio test runs the same render path as the device callback.

Both installers derive their selectable plug-ins from the build catalogues. Each plug-in has a stable four-character code and a display category. Windows Setup passes the selected codes to a separately elevated VST3 installer. Legacy migration runs only for codes that were successfully installed. Apple Installer associates each code with its own component package and receipt. A plug-in catalogue entry can declare a runtime dependency on the standalone application with `requiresStudio`. Both native checklists select and lock the application while any such plug-in is selected. Its checkbox becomes editable again when the last dependency is cleared.

Both Windows installers open with the application and all VST3s selected. Manually choosing the DAW preset selects the workstation VST3 and the bundled devices referenced by the Paper Lantern Replies production session. Setup shows the plug-in as **OSTF Studio (VST3)** to distinguish it from the optional standalone application. External hosts discover the instrument by its existing name and class ID.

| Module | Responsibility |
|---|---|
| `ScoreDocument` (future) | exact official OSTF 5.0 parsing/validation; no invented fields |
| `ProjectPackage` (future) | exact official OSTFP containers and checksums, migration |
| `GlobalTimeline` | unified score lanes, source vs performance vs clock semantics |
| `PerformanceEngine` | deterministic scheduling; loops and finite plans |
| `ProductionEngine` | VST3/AU hosting, MIDI/audio routing/mixer |
| `DeviceGraph` | ASIO/WASAPI/CoreAudio multiple-device aggregation |
| `AIEditEngine` | proposed edits, diff preview and approve-at-boundary |
| `DistributedSession` | remote state synchronization with jitter strategies |
| `Exchange` | Song export to SMF 0/1, selected-track MIDI drag and lead/chorus export to Synthesizer V. Range-based OSTF TEXT merge is planned. |

## Principles
1. Score/source coordinates must not be overwritten by playback loop/sync time.
2. Real-time audio threads must never block for AI, network, file I/O, logging or UI.
3. Keep third-party plugin state separate from musical intent; do not bundle plugin executable binaries.
4. Repeated passes and local performance variations must not mutate original score unless explicitly committed.
5. Each external conversion reports unsupported expression fields and warnings.
6. Optional audio is a supporting capability, never a prerequisite for OSTF exchange.
7. Windows-first verification, macOS cloud compile; no Mac real-hardware claims absent independent test.
8. Test actual plugin/device compatibility instead of asserting it.

## Transport and song information

The piano roll edits source clip events by OSTF note ID. A pointer gesture previews locally and commits the affected notes together on release. Reprojection updates the arrangement, note lanes and transport input; the existing mixer and plug-in instances stay attached. Drum events keep their sound-slot identity. A drum pitch change resolves a key within the track's kit. External note references protect deletion and cross-clip transfers. Note undo history checks the exact score revision before restoring it. Global chord voicings remain informational chord snapshots.

The selected tempo lane uses the lower editor. A ramp gesture replaces tempo events in the dragged interval with ordered OSTF 5.0 tempo changes. The source tempo map, OSTF/OSTFP serialization and MIDI conductor export use the resulting points. Each gesture is one score edit. The horizontal defaults strip opens a tabbed dialog that applies metadata, credits and musical settings as one transaction. The native window caption follows document identity and unsaved edits. Arrangement row sizes exclude the fixed 16 px rows used for chord and scale fit.

The source tempo map integrates quarter-note BPM over successive tick intervals. The preview engine uses it to schedule notes and program changes in samples and to convert the device sample counter back to musical time for recording, plug-in playheads and all views. The transport displays elapsed 30 fps non-drop timecode from that sample counter. The musical counter uses the denominator of the source meter. The current reader also accepts meter changes on bar boundaries when the number of ticks per bar is preserved. The piano roll uses the same tick-to-pixel transform for notes and the playhead.

Seeking uses the same tempo map for the instrument timeline, plug-in playheads and WAV position. The message thread reconstructs program changes and held pitched notes per destination and MIDI channel before the audio block resumes. The device callback lock protects the cursor and processor reset during the handover. Stop retains the final cursor for the next play or recording. The section readout uses the source section intervals even when their lane is hidden. Arrangement and piano-roll follow move the shared time viewport.

The key, chord and scale readouts use unfiltered source regions. A chord ends at its exclusive end tick. General scale regions can overlap, and the readout shows each active label. A target-specific scale only belongs to its authored scope. The song scale supplies the fallback. Song information edits update the existing score, retaining ordered credit IDs and contributor identities, localized metadata, notes and other fields. The package writer updates its manifest title and score digest on save.

## Notation and transcription

The live section matrix selects a track and a half-open source tick interval. Membership for note editing uses the note onset. The end can extend past the interval and neighbouring notes remain visible when the user scrolls. Arrangement and live edits share note IDs and undo. Session scrolling has its own bar range. Matrix header controls call the same track routing and plug-in actions as the arrangement.

Studio tracks MIDI-note ownership and the adopted take on source tick intervals. Existing notes default to T1. The optional Studio extension `org.ostf.studio.midi-takes` carries alternate MIDI performances in OSTF/OSTFP. The standard note enabled flag projects the adopted performance for playback and SMF export. Authored disabled notes remain muted. OSTF 5.1 interchange awaits the formal specification.

Live audition overrides are transient. Preparing MIDI streams and seek events occurs on the message thread. At section entry the engine swaps the prepared streams and publishes the sounding revision. Committing the audible combination records one score edit. The engine preserves the clock and current pass when appending or duplicating sections, including preparing new tempo, meter and audio data. Audio take lanes, shared clip cuts and hot track topology changes remain open. [Studio take specification and acceptance scenarios (Japanese)](TAKES_AND_LIVE_SESSION.ja.md)

Score view, print preview and printed staves draw harmony from the song timeline and lyrics from the chosen voice, independently of the notated part. Written phrases and note-linked pronunciation use two separate lines when both are enabled. The print layout counts visible credits when calculating the first page's available staff area.

Arrangement, Lead sheet and Score share the main workspace and transport. A single embedded notation view reads a projection of the open song. Source ticks define bar boundaries, chord and scale spans, lyric timing and note durations. The transport timer supplies the sample-derived position to both reading modes. The grid fits rows to the current viewport and follows the active bar unless the user turns off follow. A selected lyric voice and optional pronunciation come from the OSTF data. The derived chord voicing is available as a part. Staff and TAB choices belong to the open view.

Screen and print have distinct layouts and share musical content and drawing routines. The live view uses a moving playhead and highlights lyrics and harmony. PDF export snapshots the selected layers and renders printable A4 pages off the message thread. MusicXML 4.0 exports one selected part, preserving tick durations and splitting sustained notes across bar lines with ties. Standard tunings are E–A–D–G–B–E and E–A–D–G, low to high. Fingering is indicative.

Media import builds a candidate through the existing OSTF reader. SMF type 0/1 import groups notes by source track and channel and maps channel 10 to percussion. The audio score estimator decodes registered formats, finds simultaneous spectral peaks and onsets, and assigns provisional part roles. Isolated tonal frames are compared against the same GM family harmonic profiles used to generate the bundled preview SoundFont. A part gains a timbre label only on a clear match covering a majority of its note duration. Its 120 BPM, 4/4 grid is a guide. The separate WAV-link action supports recorded clips.

Next engraving milestones are rhythmic spelling and beams, enharmonic spelling in context, better percussion mapping and editable fingerings, followed by a combined score and round trips in external notation editors. For transcription, measure onset and pitch accuracy against annotated recordings before adding tempo inference or attempting polyphonic separation. Keep estimates inspectable before the user saves them. Exercise changes with the notation smoke test, independently validate MusicXML against the [W3C 4.0 schema](https://www.w3.org/2021/06/musicxml40/), and validate imported OSTF against the pinned Core schema.

## User-visible milestones

| Milestone | Deliverable and verification |
|---|---|
| M0 · Bootstrap | Build the JUCE GUI and run the standalone C++20 tests on Windows x64 and macOS arm64. On each platform, launch the unsigned preview, resize the demo timeline, and exercise valid, invalid, canceled, and unreadable JSON inspection. |
| M1 · OSTF TEXT | After official release intake, implement validation and read-only mapping to section, chord, key, meter, tempo, and phrasing lanes. Add text export, clipboard and range exchange, and a merge preview with undo and explicit approval for destructive edits. Verify with official positive and negative fixtures and semantic round trips. |
| M2 · Projects and playback | Implement OSTFP load/save with the official manifest and digest rules. Test optional audio, missing assets, unknown-data handling and save recovery. Keep source musical positions, finite repeat/loop plans, and session/audio clock separate. Verify deterministic rendering and SMF Type 1 export with independent MIDI readback. |
| M3 · Hosting and sound | Compare JUCE and Tracktion Engine for licensing, state persistence, routing, and deterministic scheduling. Implement the chosen host boundary and test VST3 discovery, state restoration and missing plugins, AU on macOS where supported, and baseline playback on real Windows and Mac devices. |
| M4 · Vocal exchange | Implemented SVP version 113 export of the lead, chorus, or both. Verify import and rendering in the commercial editor. Vocal expression conversion and automated integration remain open. |
| M5 · Audio devices and AI edits | Measure Windows ASIO input and WASAPI output with independent clocks, and verify CoreAudio on macOS. Test buffering, resampling, drift and latency compensation. Validate AI proposals and diffs off the audio thread and apply approved edits at safe musical boundaries. |
| M6 · Collaboration and distribution | Test edit ordering, reconnect, conflict handling and transport under simulated WAN latency. Complete licensing, signing, notarization and real-device release verification. |

### Official OSTF 5.0 release intake

The desktop reader dispatches standalone scores by exact version. The 3.5.0 and 4.0.0–4.3.0 import paths validate the original bytes using the corresponding embedded structural schema. OSTFP 1.0.0 can also carry a standalone 4.3.0 score. Associated historical session profiles require separate migration. The working preview derives its span from the authored regions and event extents when the nominal duration is omitted. Saving writes Core 5.0.0, preserves authored score fields and records the previous revision and byte hash when the source declares document identity. Historical songs use the same bar projection and track capacity as current scores. Full semantic conformance and variable-meter playback remain open.

1. Obtain the final specification and errata, root OSTF and referenced profile schemas (including vocal, production and distributed profiles where applicable), OSTFP package and manifest rules, hash and canonicalization rules, and official score/package examples plus any available conformance tests. Use the exact filenames and references from that release.
2. Record release provenance, revision, checksum and redistribution terms. Check that the schemas and all references resolve and confirm the official samples against the matching validator before writing adapters.
3. Document how the official types project onto the timeline, how source data survives an import/export round trip and what diagnostics are needed for unsupported data. Keep hardware identifiers and credentials in OS-local settings.
4. Build the parser and validator behind a portable core interface, then implement the text, timeline, and package milestones above. Use official fixtures as conformance evidence and add regression cases for each implemented rule.

### Audio clip timing

An audio track has one WAV region. Its timeline duration controls how long it sounds. The clip editor displays one complete source cycle. Both the waveform thumbnail and the audio reader use the same ordered warp anchors. A native loop repeats in source seconds. A warped loop repeats in score ticks and follows tempo changes. Linear single-pass warps use Production 0.3 fit with followRate. The Studio host extension carries loop length and interior marker pairs for readers that implement them. Exported assets keep their source samples.

External sample drops use the local WAV import path. Installed Melodyne and Splice plug-ins use the regular VST3 browser. The Studio VST3 transport follows host sample time through the OSTF tempo map. ARA document hosting remains open.

## Import review and drum input

Score analysis runs on a background worker. Its review window displays the candidate’s chord regions and note tracks, supports individual cross-window drops and can hand the complete in-memory score to the originating editor as an unsaved song. Local PDF recognition uses Audiveris and the shared MusicXML reader. A connected AI provider can read page images.

The ordered sound-slot membership of an OSTF drum kit drives both editors. The piano roll indexes distinct mapped pitches. Kit edits materialise implicit memberships and isolate shared assignments before remapping events. MIDI recording routes mapped notes to the selected instrument and stores sound-slot references with recorded velocities and timing.

### Recording inputs, MIDI patches and analysis modes

The recorder resolves a physical channel against the current active input mask before a take. Studio stores the first input channel, channel count and digital gain in its project routing extension. A peak monitor on the shared audio device manager measures inputs before transport mixing. Tracks using the same source have independent gain and meters.

Initial and timed patches are OSTF clip MIDI events. The patch dialog writes CC 0, CC 32 and Program Change with same-tick ordering. At a seek or loop boundary the host replays the bank in effect at the last program change, followed by any subsequently staged bank. MIDI-capable track inserts receive independent copies of the patch messages. Cue switches use the existing mixer send. The tempo graph shares the arrangement ruler bounds and its visible bar range.

AI focus and model selection live in Studio preferences. Each focus supplies musical guidance to the existing schema-constrained import analysis. Refresh uses the official CLI model catalog with the isolated Studio sign-in.

### Shared MIDI instruments and clip timing

A MIDI track's instrument menu selects an existing instrument track and a MIDI channel together. The source track sends its notes, patch changes and controllers to that part. Open shared instrument opens the destination editor. The instrument returns its stereo audio on the hosting track. Each source retains its own notes. Studio saves the destination track ID and channel in the session mix.

The instrument menu on the host or one of its parts can append an empty track on the next unused channel, wrapping from 16 to 1. The new Core track declares its channel, and the Studio session routes it to the owner. A shared GM part on channel 10 uses a drum track. Playback and seek use the assigned part channel for notes, controllers and patch changes on the owner and each sending track. A track redirected from its own instrument retains its local assignment and releases that instance. The returning track can reload its former assignment. Audio and inserts for the shared stereo output belong to the owner.

Neon Platforms saves two instrument devices with the same GM VST3 class ID. Guitar hosts five channels and bass hosts three. Each part's header and shared editor title show the owner's track number. Opening another part of the same group reuses that owner's editor. The Sharing menu offers individual detach on a sending track and individual or group detach on the host.

First Light Vector uses the Studio clip timing extension on the Transition Sweep region. The cycle is four quarter notes and the region repeats three times. Two interior warp points define the same source mapping on every cycle. OSTF 5.0 carries these settings in the Studio extension. The OSTFP Production Session 0.3 document carries them in its Studio host extension. A host must understand that extension to reproduce the non-linear timing.

The GM Sound Module accepts bank MSB/LSB followed by program change, including melodic selection on channel 10 and percussion on channels 10 and 11. It handles GM System On, GM2 System On, master volume/tuning and part tuning controls. The bundled FluidR3 bank supplies a subset of GM2 variations. Unavailable variations fall back to the base program. The module makes no GM2 conformance claim. Full sound-set coverage, controller destinations and effects require additional implementation and verification.

The compressor's Listen button measures three seconds of local input. Its proposed threshold targets three dB of static reduction at the 90th percentile of active 20 ms peaks, using the current ratio. Apply updates the existing threshold parameter. Silence produces no suggestion.
### Vocal parts and lead-sheet text

The display-lyric lane and lead sheet use lyric phrases in the primary lyric document at their authored score positions. Phrases without explicit timing inherit the extent of their units or aligned notes. The piano roll shows the selected voice's document and its note-aligned pronunciations. A read-only overlay draws the other vocal melodies. Track selection, editing and playback keep their original track identities. Paper Lantern Replies restores separate Formant Singer instances for the lead and backing parts.

The factory vowel and consonant recordings are synthesized from HTS Voice Mei offline and compiled into the VST3. Singer decodes the syllables and finds pitched sustain loops when the factory bank is first loaded. The audio callback plays the selected syllable at note pitch. Imported user recordings take precedence. The model credits and CC BY 3.0 notice accompany the plug-in.

# OSTF Studio — architecture proposal (not normative OSTF spec)

## Shared instruments, sample placement and stem exports

The shared instrument owner receives channel-remapped events from its routed parts. Its stereo return is mixed once. The GM editor reads cached bank metadata and publishes channel activity from MIDI onsets and sounding synth voices. Each editor part selector edits a single channel while all channels can play. Detaching a child removes its route and reactivates its prior instrument assignment if available.

The folder toolbar button and the right pane use the same sample browser. File drops resolve to arrangement time. Moving a clip changes its start tick and keeps its source and warp data. Existing audio tracks accept one clip and require confirmation to replace it. The Stem Separator VST3 passes its input through, records bounded mono or stereo host audio on request, and analyses file or captured input on a background worker. Studio's importer and the VST3 share the local complementary stem estimates and note/drum/chord analysis. Each completed result is a directory of WAV and standard MIDI files. The chord MIDI carries playable triads and timed text markers under a named track so that Studio can also place it on its chord lane.

## Factory instruments and the sample browser

Four separately identifiable VST3 instruments share a polyphonic rendering engine. FM uses phase modulation, analog uses band-limited saw oscillators, the drum machine models pitched and noise transients, and wavetable synthesis interpolates harmonic-limited tables. The processor takes MIDI events at sample offsets and captures the active preset per voice. Its VST3 state holds preset, tone and output level. First Light Vector stores program changes in the score and instrument class references in the production session.

The right pane uses the JUCE directory browser for local audio. Drag payloads capture the selected file path, and internal and operating-system drops share track placement, replacement confirmation and asset limits. Performance numeric entry uses the same score edit, undo and playback paths as drawn points.

## Score interchange

MusicXML partwise documents and compressed MXL containers feed the same in-memory score import and validated OSTF save path. Local PDF recognition passes the Audiveris MXL output directly to this reader. Full-song MusicXML exports the source notation parts, with harmony on the first part. ChordPro exports chord symbols with aligned lyrics. These are score interchange views of the current project.

## Mixer and plug-in controls

The host scopes its console LookAndFeel to the main component. Workspace preferences store a stable finish key, while musical degree colours and status indicators retain their semantic palettes. Mixer pan, send and level controls use the same channel callbacks as the track headers. The GM instrument and stock effect editors own their own LookAndFeel instances so they render consistently in other VST3 hosts. The effect editors attach each control directly to its existing ranged parameter for gestures, host automation and state recall.

## MIDI control surfaces

Mackie Control reserves a user-selected OS MIDI port. Channel-strip and transport messages enter a bounded queue and the UI thread applies them to the current eight-track bank. Musical MIDI routes ignore the reserved port. An optional output sends changed fader positions, pan ring values and button states. Both port identifiers live in local preferences.

Record arm reuses the transport's live MIDI filter, destination and render chain. Stopped monitoring processes the armed destination while the score cursor stays parked. Hardware destinations dispatch timestamped MIDI, bypass track audio processing, and release held voices on stop. The MIDI input lamp and post-fader audio meters report different signals.

## Media analysis and external editors

Source tracks have an exclusive record arm. Each MIDI track filters incoming channel messages by its selected port and channel before updating the input lamp. A recording snapshots the input filter for monitoring and capture. Port selections are stored locally by document and track ID. Audio track rows consume zero height for harmonic fit lanes.

The Mode menu selects the arrangement, lyric or vocal guidance used for AI analysis and remembers it in local Studio preferences. A read-only prompt guide beside the global timeline headings cycles through three local examples for that mode at 15-second intervals and restarts the interval on manual navigation. The upper toolbar holds the ChatGPT connection control and, when connected, the model selector and catalog refresh action.

The import window snapshots the selected provider and model for each worker job. The model selector offers automatic selection and the visible models in the Studio CLI profile’s catalog. An explicit selection is passed to the CLI for analysis. Local code decodes source events. For cloud analysis an AnalysisProvider adapter performs account checks and schema-constrained musical requests. The ChatGPT adapter delegates sign-in to Codex CLI. Studio starts the official browser flow in a private CLI profile and checks that profile for ChatGPT authentication. The sign-in button can reopen the pending authorization URL. Disconnect signs out that profile. A local preference stores Studio opt-in. Long dropped parts extend a temporary score through the last converted event and only replace the live score after successful placement. Audio note candidates are assigned to parts in batches, then MIDI or audio pitch profiles are analysed for harmony. Native Windows and macOS PDF rendering supplies page images for AI. Local PDF recognition reads the MXL produced by Audiveris from a temporary copy. Both routes validate the resulting in-memory score before offering track placement or document replacement. Audio FFT windows are read in bounded overlapping pages. Windows PDF analysis renders one page per worker process with Windows.Data.Pdf and passes the PNG back to the parent before starting the next page.

Splice Sounds hosts the installed VST3 editor in a scrollable tab of the plug-in browser and runs a separate AudioProcessorPlayer while the tab is visible. Its sign-in, account library and licences belong to Splice. Melodyne standalone receives a private working WAV; Studio verifies the source hash, sample rate and frame count on return.

## Editor recording

The device sample clock drives the editor cycle and one-bar count-in. The count-in holds the score cursor and excludes input from the take. MIDI note-offs are paired on an elapsed sample counter and split into score-time intervals across wraps. Audio copies the first enabled inputs into a bounded buffer allocated before the device callback starts. A new pass replaces covered samples in that buffer. Stop encodes 16-bit PCM on the message thread and applies the resulting region. Clip warp and source timing belong to the region. There is one audio region per track in this preview.

Notes, velocities and ordered kit membership use Core 5.0. WAV assets and regions use the Production profile. The existing Studio host extension stores the editor recording cycle and per-track capture width. The local device preferences hold the MIDI input-to-output note map. The user can share a project with different hardware.

## Import results and editing

The import panel holds a draft in memory. Opening the draft loads it in the original Studio window. Dragging a result converts its source ticks through seconds into destination ticks at the drop position. Chord changes use the same schema-validated edit transaction as the global timeline editor. The backing audio files remain available for later playback and explicit project saves.

## Overall product distinction

The song settings editor writes the authored key and scale to the song and edits the initial tempo and meter in the canonical timeline. Whole-score transposition and tempo scaling build a candidate score and validate it before replacing the active model. The score edit history also holds these operations. Audition transposition and tempo scaling belong to the open transport. MIDI playback and the transport display use the temporary values, while file and MIDI export use the authored score. WAV playback keeps its native sample rate.

The piano chord strip shares the note grid's time axis. During a pitch or timing drag the editor analyses the selected track's prospective notes on the UI thread. The selected-note percentages weight its intersections with harmonic intervals by duration. Arrangement fit rows report the aggregate for that track. Committing or cancelling a drag discards the temporary analysis.

- OSTF = portable musical structure (score and musical intent), independently portable.
- OSTFP = container for song + optional production/session/assets, default save project.
- DAW runtime = audio device, hosting, transport, timeline, editors, clipboard, AI integration.
- OS-local settings = hardware device IDs, auth, user-specific settings, plugins scanning paths.

Toolbar and track-header actions share the score editing, mixer routing and plug-in hosting paths. The mixer supports eight buses and eight returns with stable saved output slots. Removing a bus redirects its tracks to Master. Removing a return clears its sends. Interface scale is stored in the local view preferences.

## Project lifecycle

A new session reads the embedded empty OSTF template through the same reader as a standalone score. The six playable Studio demos use the OSTFP reader. The upstream comprehensive example remains a parser regression fixture. Launching a document from the operating system opens that document directly in the workspace.

## Independent modules

The proposed Composer and part arranger VST3s each embed the same host-independent C++ library for chord analysis and progression generation. Each instance keeps its own working progression. Existing OSTF chords are imported with the selected phrase, including their timing, bass and voicing. Gaps can be analysed from notes or filled with a musical suggestion. The user auditions and accepts the result before generating a part. Placing the chosen progression on Studio’s global chord lane lets multiple arrangers use it through read-only OSTF Plugin Context Exchange snapshots. External hosts can work with OSTF files, MIDI files or captured MIDI. The current preview still generates a fixed phrase. [Arranger workflows and acceptance criteria (Japanese)](ARRANGER_PHRASE_EXCHANGE.ja.md)

The planned native editor aligns chords and notes on one beat ruler. A part can regenerate a selected range using the surrounding phrase as context. The processor owns the accepted and alternate performances across editor lifetimes and project saves. Analysis and generation run outside audio processing. Playback switches prepared takes at a loop boundary, and both export paths read the accepted notes.

The planned Studio chord comparison keeps the current score visible alongside a proposed progression. The default acceptance changes chords alone. Optional relative assignments and generation dependencies identify parts for review. Users can audition and adopt selected part changes together with the progression as one undoable edit. Opening a score plays the saved notes.

A separate optional arrangement overlay estimates how the sounding notes fit each chord. It can evaluate hand-played clips without generation history. It marks intervals with enough pitched evidence, gives a reason on selection, and groups audition targets at song-wide zoom. The design caches results per interval and recalculates affected ranges off the audio thread.

Selecting a chord highlights its span across clips. The inspector reports the share of overlapping pitched note duration in the chord and in each applicable OSTF scale. Held notes contribute only their overlap on either side of a boundary. Scale targets restrict which notes and spans enter the denominator. Missing or overlapping scale contexts remain visible.

The arrangement and piano roll share a derived Theory Guide. The arrangement groups recognised progressions with brackets on the degree row and shows harmonic functions on a second row. The same-key, contiguous chord matcher includes plagal and deceptive resolutions and I–vi–ii–V–I or iii–vi–ii–V–I turnarounds. A dominant seventh a semitone above an adjacent tonic can stand in for V. A diatonic IV or iv can occupy the predominant position. An adjacent diatonic target in the same key can identify an applied dominant seventh. A preceding minor or diminished-fifth ii relative to that target extends the reading to a local ii–V. All pitched parts display separate duration-weighted chord and scale statistics at every track height. Percussion and audio have an unscored indicator. The chord denominator spans the chord even when a scale changes inside it. Scoped scales use section, chord, track, clip or note identities. An explicit improvisation scale is available as a local reference. An ambiguous overlap can be inspected one reference at a time. Missing context and silence have no percentage. Chord and scale views label the active reference. Degree fills use the chord root or scale tonic. A separate stripe identifies the chord role. Authored compound extensions take precedence over the sustained minor-ninth avoid heuristic. Its hit testing and edits still operate on whole source notes. The UI builds the interval cache when a guide is enabled and updates a part after note edits. Playback reads cached results and repaints the cursor strips until the viewport scrolls. A/B comparison and acceptance of an arranger proposal remain planned.

Drum slot editing retains the source slot ID alongside the MIDI mapping. The step view uses the existing note transaction and computes ratchet boundaries from the ends of each grid cell. Named slots can share a MIDI key. A slot without a mapping remains editable and requires a mapping for MIDI output. Clock-based loop playback splits a device block at the selected range boundary. The message thread prepares the controller and held-note state for a seek or loop restart. While a drum loop plays, slot edits stage a new MIDI timeline. A buffer swap at the next loop boundary keeps the device sample clock and existing plug-in instances running.

MIDI performance points retain their clip-local event ID and optional channel. The editor presents absolute song ticks, then resolves the destination clip on commit. Production channel pan uses a separate step or linear envelope keyed by source track ID, resolved to the saved production channel on export. It applies to rendered audio before routing and sends. The inspector reads the same harmonic role colours as the note underline, and selection is drawn over the note fill.

The preview renderer publishes stereo sample peaks from the processed source audio, output stems and the independent Cue pair. The audio thread accumulates peaks in fixed-size lock-free mailboxes. The UI consumes each peak once per refresh and shares it with the mixer and arrangement. A shared instrument's combined audio is measured on the track that owns it. Track heights and vertical scrolling are view state scoped to the open song. Group height changes use the score track kind, global note role or output destination. New tracks inherit their group height. The bar ruler stays fixed. A single horizontal range and scrollbar drive the arrangement and piano roll, including both rulers, note editing, the playhead and the velocity lane. The inspector plus piano keys occupy the same width as the track headers. A draggable divider controls the vertical split. Quantize, velocity ramps, pencil input, triplet subdivision and chord strumming use the transactional note-edit and undo paths. Pencil drags commit on release. Repeated strokes skip occupied onsets at the painted pitch. Each fractional grid boundary rounds against the project origin so triplets stay in time at non-divisible PPQs. Subdivision clones two events before shortening the original, retaining its ID and assigning new IDs to the copies. Each selected chord uses one total strum spread in quarter-note time. Equal pitches share an onset, and the spread is capped by the shortest member and the next selected onset. Original release times are preserved. The local workspace theme controls arrangement clips, harmonic row shading, note fills and the inspector legend. The score and MIDI export are independent of this preference.

Wave Analyzer uses the same catalog and a dedicated VST3 component/editor. A mono or stereo insert reads host samples and meters 100 ms hops with continuous K-weighting, 400 ms overlapping loudness blocks, a 3 s window, a bounded 0.1 LU relative-gate histogram and a four-phase true-peak FIR. A nonblocking snapshot transfers the latest waveform and readings to the editor. The message thread calculates calibrated per-channel Hann spectra and draws stereo phase. UI refresh, freeze and editor lifetime are independent of the measurement clock.

The nine stock audio effects use separate VST3 component classes and share a JUCE DSP library. The demo generator, packaging checks and Studio's VST3 identity mapping use the same effect catalog. The Production Session describes owned instruments and stock effects in processing order, bus destinations, gains, pan and post-fader sends. The Studio host extension carries edited stock effect state and accommodates legacy insert lists. The synthetic chord-voicing row keeps its settings in the extension. The offline audio test runs the same render path as the device callback.

On Windows, the per-user Studio installer and the machine-wide VST3 installer have separate ownership. Users can update the application while a DAW is using the shared plug-ins and defer shared installation. Legacy plug-in migration runs after the shared installer succeeds.

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
| `Exchange` | Song export to SMF 0/1 and selected-track MIDI drag. Planned conversions include SVP and OSTF TEXT import/export and merge. |

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
| M4 · Vocal exchange | Version the SVP mapping, report unmapped expression, and verify manual import in Synthesizer V. Evaluate ARA and any automated integration using documented vendor APIs. |
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

External sample drops use the local WAV import path. The Melodyne external-editor workflow exports a separate working file and checks source identity, sample rate and frame count before applying edited samples. ARA document hosting remains open.

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

First Light Vector uses the Studio clip timing extension on the Transition Sweep region. The cycle is four quarter notes and the region repeats three times. Two interior warp points define the same source mapping on every cycle. OSTF 5.0 carries these settings in the Studio extension. The OSTFP Production Session 0.3 document carries them in its Studio host extension. A host must understand that extension to reproduce the non-linear timing.

The GM Sound Module accepts bank MSB/LSB followed by program change, including melodic selection on channel 10 and percussion on channels 10 and 11. It handles GM System On, GM2 System On, master volume/tuning and part tuning controls. The bundled FluidR3 bank supplies a subset of GM2 variations. Unavailable variations fall back to the base program. The module makes no GM2 conformance claim. Full sound-set coverage, controller destinations and effects require additional implementation and verification.

The compressor's Listen button measures three seconds of local input. Its proposed threshold targets three dB of static reduction at the 90th percentile of active 20 ms peaks, using the current ratio. Apply updates the existing threshold parameter. Silence produces no suggestion.

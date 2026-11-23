# OSTF Studio — architecture proposal (not normative OSTF spec)

## Overall product distinction
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

The proposed Theory Guide lane groups harmonic functions and ii–V–I or V–I spans on the global timeline. It shows independent chord and scale readouts for the selected part with equal prominence. Track-level bands can display either or both. The scale may be a harmonic context or the part’s improvisation direction. When replacing an existing performance, the guide previews the original and proposed take against the same accompaniment. A change to global chords also previews the affected functional analysis. Dismissal leaves the score intact and applying a change creates one undo step.

The preview renderer publishes stereo sample peaks from the processed source audio, output stems and the independent Cue pair. The audio thread accumulates peaks in fixed-size lock-free mailboxes. The UI consumes each peak once per refresh and shares it with the mixer and arrangement. A shared instrument's combined audio is measured on the track that owns it. Track heights and vertical scrolling are view state scoped to the open song. Group height changes use the score track kind, global note role or output destination. New tracks inherit their group height. The bar ruler stays fixed. Each horizontal zoom slider sets a continuous visible range from the whole song down to one bar and shares its scrollbar with the renderer.

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

The source tempo map integrates quarter-note BPM over successive tick intervals. The preview engine uses it to schedule notes and program changes in samples and to convert the device sample counter back to musical time for recording, plug-in playheads and all views. The transport displays elapsed 30 fps non-drop timecode from that sample counter. The musical counter uses the denominator of the source meter. The current reader supports a single meter. The piano roll uses the same tick-to-pixel transform for notes and the playhead.

Seeking uses the same tempo map for the instrument timeline, plug-in playheads and WAV position. The message thread reconstructs program changes and held pitched notes per destination and MIDI channel before the audio block resumes. The device callback lock protects the cursor and processor reset during the handover. Stop retains the final cursor for the next play or recording. The section readout uses the source section intervals even when their lane is hidden. Arrangement and piano-roll follow share a switch while keeping independent scroll and zoom ranges.

The key, chord and scale readouts use unfiltered source regions. A chord ends at its exclusive end tick. General scale regions can overlap, and the readout shows each active label. A target-specific scale only belongs to its authored scope. The song scale supplies the fallback. Song information edits update the existing score, retaining ordered credit IDs and contributor identities, localized metadata, notes and other fields. The package writer updates its manifest title and score digest on save.

## Notation and transcription

Arrangement, Lead sheet and Score share the main workspace and transport. A single embedded notation view reads a projection of the open song. Source ticks define bar boundaries, chord and scale spans, lyric timing and note durations. The transport timer supplies the sample-derived position to both reading modes. The grid fits rows to the current viewport and follows the active bar unless the user turns off follow. A selected lyric voice and optional pronunciation come from the OSTF data. The derived chord voicing is available as a part. Staff and TAB choices belong to the open view.

Screen and print have distinct layouts and share musical content and drawing routines. The live view uses a moving playhead and highlights lyrics and harmony. PDF export snapshots the selected layers and renders printable A4 pages off the message thread. MusicXML 4.0 exports one selected part, preserving tick durations and splitting sustained notes across bar lines with ties. Standard tunings are E–A–D–G–B–E and E–A–D–G, low to high. Fingering is indicative.

Media import first builds a new OSTF candidate, runs it through the Studio reader, and offers an explicit save. WAV references use the existing relative media writer. Optional monophonic transcription estimates pitch in short windows, drops unstable segments and keeps the original recording alongside the draft. Its 120 BPM, 4/4 grid is a guide. MIDI import splits each SMF track by MIDI channel and maps channel 10 to percussion sound slots. It currently accepts PPQ type 0/1, a fixed meter and the supported project size. Controller and program events are future import work.

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

The desktop reader dispatches standalone scores by exact version. The 3.5.0 and 4.0.0–4.3.0 import paths validate the original bytes using the corresponding embedded structural schema. OSTFP 1.0.0 can also carry a standalone 4.3.0 score. Associated historical session profiles require separate migration. The working preview derives its span from the authored regions and event extents when the nominal duration is omitted. Saving writes Core 5.0.0, preserves authored score fields and records the previous revision and byte hash when the source declares document identity. The fixed-meter, eight-source-track and 256-bar projection limits also apply to historical songs. Full semantic conformance and variable-meter playback remain open.

1. Obtain the final specification and errata, root OSTF and referenced profile schemas (including vocal, production and distributed profiles where applicable), OSTFP package and manifest rules, hash and canonicalization rules, and official score/package examples plus any available conformance tests. Use the exact filenames and references from that release.
2. Record release provenance, revision, checksum and redistribution terms. Check that the schemas and all references resolve and confirm the official samples against the matching validator before writing adapters.
3. Document how the official types project onto the timeline, how source data survives an import/export round trip and what diagnostics are needed for unsupported data. Keep hardware identifiers and credentials in OS-local settings.
4. Build the parser and validator behind a portable core interface, then implement the text, timeline, and package milestones above. Use official fixtures as conformance evidence and add regression cases for each implemented rule.

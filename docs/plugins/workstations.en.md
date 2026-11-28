# Composition and instruments

[日本語](workstations.md) · [All plug-ins](en.md) · [Studio user manual](../manual/en.md)

Load the workstation or a playable instrument in your DAW’s instrument slot. To edit the notes of a Studio project in the development build, open an OSTF Tool from a compatible track’s wrench inside Studio.

[OSTF Studio](#ostf-studio) · [OSTF Scale Editor](#ostf-scale-editor) · [OSTF Velocity Shaper](#ostf-velocity-shaper) · [OSTF Guitar Voicer](#ostf-guitar-voicer) · [OSTF Note Split](#ostf-note-split) · [OSTF Motif Expander](#ostf-motif-expander) · [GM Sound Module](#gm-sound-module) · [OSTF Formant Singer](#ostf-formant-singer) · [FM Synth](#fm-synth) · [Analog Synth](#analog-synth) · [Wavetable Synth](#wavetable-synth) · [Drum Machine](#drum-machine) · [Instrument Rack](#instrument-rack) · [SUPER KNOBBER LEAD](#super-knobber-lead)

## OSTF Studio

The complete workstation on a stereo instrument track. Choose **OSTF Studio (VST3)** in the Public Alpha 2 installer, open Paper Lantern Replies inside the plug-in, and start your host transport to hear the arrangement.

![OSTF Studio showing DAW transport follow above the arrangement and piano roll](../images/plugins/ostf-studio.png)

[Signal path and controls](ostf-studio.en.md)

## OSTF Scale Editor

The development build also accepts Scale Editor in the global wrench beside the prompt arrows and the voicing and audio track wrenches. These slots show compatible tools.

In the development build, choose **Tools → OSTF Scale Editor** on the MIDI track whose instrument you want to hear. Older projects that used Scale Editor in the instrument slot retain their saved audition instrument.

In Public Alpha 2, load **OSTF Scale Editor** on an instrument track **inside OSTF Studio**. In another DAW, load **OSTF Studio (VST3)** first and open the project there. Choose a chord in the left pane, audition candidates, then choose **Set scale**. The check mark and scale name show the assignment for the selected target. **Harmony** updates Studio’s global scale lane. A pitched track receives its own scale direction. Save the project in Studio or in the DAW containing it.

Select the section in the header and click **Play section**, or choose **Play song** to start at the beginning. The header names the sounding scale. In Public Alpha 2 the header also lets you choose the audition instrument. [Assign and audition scales](../manual/en.md#scale-assignment)

![OSTF Scale Editor showing section selector, chord list and scale comparison](../images/plugins/ostf-scale-editor.png)

## OSTF Velocity Shaper

**Windows development preview**

In the development build, click **Sections** to see the named section tabs. Choose a section, take and shape to compare the original velocities with the proposed bars. **Beat controls** exposes one vertical fader per rhythmic position.

Open the tool on a MIDI track, then choose a section, take and shape. Gentle, Beat accents and Lift apply offsets to the played velocities. Choose an immediate switch or a blend over 1, 2 or 4 beats. Apply sections in song order to blend from the preceding adopted take's shape. An unconfigured predecessor uses the original dynamics. Reapplying the same shape preserves the previously calculated velocities.

After editing sections out of order, choose **All configured section transitions** in the target list to recompute just the entrances of adopted takes in one edit.

Click **Preview edits** to inspect the count, then **Listen A** or **Listen B** through the track's local instrument. **Apply** writes the target. **To new take** copies a supported whole section in OSTF 5.1 or 5.2 and writes the result there. The original stays adopted until you choose a different take in Studio.

Choose **Piano roll · selected notes**, one **Chord** by bar and beat, a section, a take range or **Entire MIDI track**. The take selector shows the available performances for that range. A whole track spanning multiple comp regions uses the adopted performance. Choose one take range to edit an alternate. Piano-roll selection uses the notes in the displayed take.

In **Beat controls**, adjust downbeats, offbeats, contrast or limits. **Velocity offset** adds a fixed amount to the current values. The graph updates before you apply. Each grey mark shows the played velocity and each green bar shows the proposed velocity, in onset order. Hover for exact values. Dense columns show the peak. Click or drag across the graph to restrict the edit to individual notes, or double-click to include the whole target again. **Apply** writes the previewed changes in one Studio Undo step.

![Velocity Shaper comparing played and proposed velocities on a chord in Take 2](../images/plugins/ostf-velocity-shaper.png)

## OSTF Guitar Voicer

**Windows development preview**

Open **Tools → OSTF Guitar Voicer** on a pitched MIDI track. Select a section and take, set a fret range and preview. The fretboard shows the first voicing, from low E at the bottom to high E at the top. **Link positions** also considers the preceding chord when choosing a fingering. The target can be changed to the current piano-roll note selection.

The tool voices groups of 3–6 simultaneous notes from the same clip containing at least three pitch classes. Each string receives one chord tone in standard tuning. Existing gates and velocities are retained, and additional voices inherit those of the upper source note. Single notes, arpeggios and unplayable groups are counted as skipped. Audition and apply use the same section controls as Velocity Shaper.

![Guitar Voicer showing the first chord across six strings](../images/plugins/ostf-guitar-voicer.png)

Click a section tab to see its first fingering. Try Open, Low, Middle or High, or drag both ends of the range to choose exact frets.

## OSTF Motif Expander

**Windows development preview**

Open the wrench on a pitched MIDI track, add **OSTF Motif Expander**, select 1–96 piano-roll notes in one take and click **Capture selection**. The header identifies the source track, take and bar. Choose a variation style and similarity, then click **Generate**. The two lanes share a time axis and pitch range. Use **Hear source** and **Hear variation** to compare them. **AI variation** sends the captured notes and settings to the AI connected to Studio. Manage that connection from the top of any tool window.

- **New take here** copies the source clips or existing comp range in an OSTF 5.1 or 5.2 score. It changes the selected notes in the copy and preserves the adopted performance. Linked vocal notes and clips requiring dedicated performance maps can restrict take creation.
- **Place at cursor** writes the variation into the selected MIDI track and editing take. Inside a comp group, the entire phrase must fit in an empty range of the chosen take’s output clip. Both placement operations have a single undo step.

![Motif Expander showing the original phrase and the generated variation on aligned pitch and time axes](../images/plugins/ostf-motif-expander.png)

## OSTF Note Split

**Windows development preview**

Choose **Tools → OSTF Note Split** on a MIDI track. Select notes in its piano roll. A bright outline and check mark identify the selection. Each note is divided within its original duration, including when several notes overlap.

Use **Even** for a fixed count per note. **Bar gradient** cycles between two densities over a given number of bars. **Continuous** varies density over each note, with optional beat or bar alignment. The preview shows up to six source notes. Each bar's width shows its gate and its height shows velocity. **Split selection** applies to every selected note. If chance drops all strokes for a source note, Studio keeps the original. One Studio Undo restores the selection's original performance.

![A three-note selection with a bar-gradient split preview](../images/plugins/ostf-note-split.png)

## GM Sound Module

A 16-part General MIDI instrument. Send piano and bass to different MIDI channels, choose a sound for each part and watch the corresponding channel meters.

![GM Sound Module showing MIDI parts and channel controls](../images/plugins/gm-sound-module.png)

## OSTF Formant Singer

Sings the notes and lyrics of an OSTF vocal part. In an external host, use **Load OSTF**, select the vocal part and play the song. Compare **Vibrato / scoop depth** on a sustained note.

For a short phrase, try a vocal part with the English word as its display lyric and matching katakana aligned to its notes. The chorus in **Paper Lantern Replies** is an example for comparing joined consonants and sustained vowels.

![OSTF Formant Singer showing OSTF song, vocal part and vibrato depth](../images/plugins/ostf-formant-singer.png)

## FM Synth

Choose a patch to change the modulation ratio, then turn **Tone**. The display overlays the modulator with the resulting waveform at note onset.

![FM Synth — Glass Keys with overlaid modulation and carrier waveforms, a 2-to-1 ratio and a Tone knob](../images/plugins/fm-synth.png)

## Analog Synth

Turn **Tone** while playing a bass line. The amber curve shows how the low-pass response at note onset shifts toward higher frequencies.

![Analog Synth — wood-sided panel with the amber Round Bass filter response](../images/plugins/analog-synth.png)

## Wavetable Synth

Turn **Tone** to move the highlighted reference waveform through the table. Compare the movement of **Air Pad** with **Digital Stab**.

![Wavetable Synth — stacked Air Pad waveforms with the reference frame highlighted at 15%](../images/plugins/wavetable-synth.png)

## Drum Machine

A drum instrument that maps MIDI notes to percussion voices. Play the same beat with **Studio Kit** and **Tight Machine** to compare the length and tone of the hits.

![Drum Machine showing Studio Kit preset and level fader](../images/plugins/drum-machine.png)

## Instrument Rack

Layer instruments on one MIDI part. Add two devices with **+ Instrument layer**, then use each **Layer level** to blend a quiet pad under a piano.

![Instrument Rack showing instrument layers, level and serial inserts](../images/plugins/instrument-rack.png)

## SUPER KNOBBER LEAD

Play the LEAD v1.2 instrument on a MIDI track in Studio or another VST3 host. Choose a genre and preset at the top. Try **GM → GM 082 Lead 2 sawtooth**, then open **FM / Table / Filter** and turn **Cutoff** to hear the change in brightness. The host can automate each sound control directly.

**Original rate** is the default. Try **2x oversampling** on a bright, driven lead and listen to the upper notes. Both modes report 16 samples of latency.

![SUPER KNOBBER LEAD showing oscillator and source mixer page with genre and preset selection](../images/plugins/super-knobber-lead.png)

[Signal path and controls](super-knobber-lead.en.md)

[Return to the plug-in guide](en.md)

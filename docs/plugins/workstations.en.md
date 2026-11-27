# Composition and instruments

[日本語](workstations.md) · [All plug-ins](en.md) · [Studio user manual](../manual/en.md)

Load these on an instrument track to play a score, sing lyrics or turn live MIDI into sound.

[OSTF Studio](#ostf-studio) · [OSTF Scale Editor](#ostf-scale-editor) · [GM Sound Module](#gm-sound-module) · [OSTF Formant Singer](#ostf-formant-singer) · [FM Synth](#fm-synth) · [Analog Synth](#analog-synth) · [Wavetable Synth](#wavetable-synth) · [Drum Machine](#drum-machine) · [Instrument Rack](#instrument-rack) · [SUPER KNOBBER LEAD](#super-knobber-lead)

## OSTF Studio

The complete workstation on a stereo instrument track. Choose **OSTF Studio (VST3)** in the Public Alpha 2 installer, open Paper Lantern Replies inside the plug-in, and start your host transport to hear the arrangement.

![OSTF Studio showing DAW transport follow above the arrangement and piano roll](../images/plugins/ostf-studio.png)

[Signal path and controls](ostf-studio.en.md)

## OSTF Scale Editor

In Public Alpha 2, load **OSTF Scale Editor** on an instrument track **inside OSTF Studio**. In another DAW, load **OSTF Studio (VST3)** first and open the project there. Choose a chord in the left pane, audition candidates, then choose **Set scale**. The check mark and scale name show the assignment for the selected target. **Harmony** updates Studio’s global scale lane. A pitched track receives its own scale direction. Save the project in Studio or in the DAW containing it.

Select the section in the header and click **Play section**, or choose **Play song** to start at the beginning. The header names the sounding scale. Choose your audition instrument there to hear saved assignments or suggestions for an unassigned range. [Assign and audition scales](../manual/en.md#scale-assignment)

![OSTF Scale Editor showing section selector, chord list and scale comparison](../images/plugins/ostf-scale-editor.png)

## GM Sound Module

A 16-part General MIDI instrument. Send piano and bass to different MIDI channels, choose a sound for each part and watch the corresponding channel meters.

![GM Sound Module showing MIDI parts and channel controls](../images/plugins/gm-sound-module.png)

## OSTF Formant Singer

Sings the notes and lyrics of an OSTF vocal part. In an external host, use **Load OSTF**, select the vocal part and play the song. Compare **Vibrato / scoop depth** on a sustained note.

For a short phrase, try a vocal part with the English word as its display lyric and matching katakana aligned to its notes. The chorus in **Paper Lantern Replies** is an example for comparing joined consonants and sustained vowels.

![OSTF Formant Singer showing OSTF song, vocal part and vibrato depth](../images/plugins/ostf-formant-singer.png)

## FM Synth

An FM instrument for electric keys, bells and plucks. Hold the same chord with **Glass Keys** and **Bell Cascade** to hear how their attacks and decays differ.

![FM Synth showing Glass Keys preset, Tone and Output](../images/plugins/fm-synth.png)

## Analog Synth

An analog-style instrument for bass, saw leads and brass. Play a low line with **Round Bass**, then switch to **Twin Saw** to compare their harmonic character.

![Analog Synth showing Round Bass preset, Tone and Output](../images/plugins/analog-synth.png)

## Wavetable Synth

A wavetable instrument for moving pads and short stabs. Hold a chord with **Air Pad** while changing **Tone**, then play it staccato with **Digital Stab**.

![Wavetable Synth showing Air Pad preset, Tone and Output](../images/plugins/wavetable-synth.png)

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

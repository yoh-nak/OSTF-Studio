# Analysis and stem extraction

[日本語](analysis.md) · [All plug-ins](en.md) · [Studio user manual](../manual/en.md)

Inspect a mix visually or extract material from recorded audio for further editing.

[Wave Analyzer](#wave-analyzer) · [Universal Tuner](#universal-tuner) · [Stem Separator](#stem-separator)

## Wave Analyzer

Shows waveform, spectrum, stereo field, loudness and true peak. Insert it last on the master, play a passage, then use **Freeze** to inspect a moment.

The screenshot shows a stereo signal being measured. Read the loudness and true peak across the top, compare frequency peaks and stereo spread in the middle, then follow the waveform below.

![Wave Analyzer showing active spectrum, stereo field, waveform and loudness meters](../images/plugins/wave-analyzer.png)

## Universal Tuner

**Windows development preview**

Insert the effect on a monitored audio input or after a software instrument. Play one note at a time. The LCD shows its octave, measured frequency and cent offset. The marker moves left for flat and right for sharp. **IN TUNE** lights within two cents of the note.

Select an **Instrument** to see its artwork. For string instruments the matching open string lights when you play its pitch and octave. Chromatic tracking also works for alternate tunings and voice. Set **Reference Pitch** between 430.0 and 450.0 Hz, or press **440** to reset.

For silent tuning on stage, engage **Mute output**. The input meter and pitch detection stay active. Press it again to restore the track output.

![Universal Tuner measuring the open A2 string on a guitar](../images/plugins/universal-tuner.png)

## Stem Separator

Estimates drums, bass, other and vocals from a recording. Drop a file or use **Capture track** and **Finish capture**, then drag the resulting **WAV** or **MIDI** onto a track. Some sounds can bleed between the estimated stems.

![Stem Separator showing audio capture and WAV or MIDI export for the estimated stems](../images/plugins/stem-separator.png)

[Return to the plug-in guide](en.md)

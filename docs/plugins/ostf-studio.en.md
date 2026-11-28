# OSTF Studio

[日本語](ostf-studio.md) · [All plug-ins](en.md)

**Signal path.** Load the workstation on an instrument track in your DAW. It accepts host MIDI and transport position and returns the Studio mix as stereo audio. Its VST3 has no audio input or MIDI output.

![OSTF Studio controls](../images/plugins/ostf-studio.png)

1. Choose **OSTF Studio** in the DAW’s instrument browser and open an OSTFP from the workstation’s File menu.
2. Enable **Follow DAW transport** and press Play in the host. Studio renders its instruments and inserts to the host track. Turn follow off to use Studio’s transport for a separate audition.
3. To assign scales, open **OSTF Scale Editor** in a Studio instrument slot in Public Alpha 2, or from a Studio track’s wrench in the Windows development build. Saving the host project stores Studio’s project and the follow setting.

Match the initial downbeat and tempo and meter maps for identical bar numbers. A second computer needs the VST3s and external libraries used in the song.

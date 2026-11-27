# OSTF Studio user manual

[日本語](README.md) · [Illustrated plug-in guide](../plugins/en.md) · [Project home](../../README.md)

## Play a song inside your usual DAW

If you work in Ableton Live, Cubase or another VST3 host, load **OSTF Studio** as an instrument on a track in that DAW. You can edit the score inside Studio and record or mix the rest of the production in your host.

**Version availability.** The workstation VST3 was added in the development build after Public Alpha 1. With Public Alpha 1 you can open the standalone Studio application, or load individual instruments such as Scale Editor and Formant Singer in your DAW.

1. **Install the workstation instrument.** In the Windows development installer, all components start selected. Choose **Studio VST3 for another DAW** for the workstation and the instruments and effects used in the demo. The individual workstation checkbox is **OSTF Studio (VST3)**.
2. **Create an instrument track.** Rescan VST3s in your host and search for **OSTF Studio** in its instrument browser. In Ableton Live, drag it from the browser to a MIDI track. In Cubase, open **Add Track → Instrument** and select it as the instrument. [Ableton VST3 setup](https://help.ableton.com/hc/en-us/articles/209071729-Using-VST-plug-ins-on-Windows) · [Cubase instrument tracks](https://www.steinberg.help/r/cubase-pro/16.0/en/cubase_nuendo/topics/tracks_about/tracks_about_add_track_instrument_settings_r.html)
3. **Open the song inside the plug-in.** Choose **File → Demo songs → Paper Lantern Replies**, or open your own `.ostfp` project or `.ostf` score.
4. **Start your DAW at the beginning.** Enable **Follow DAW transport** at the top left of the plug-in. Studio sends a stereo mix to the host track. The lead vocal enters at bar 5. To audition with Studio’s own transport, turn follow off.

![Hosted workstation with the DAW transport follow switch above the Studio transport, arrangement and piano roll](../images/plugins/ostf-studio.png)

The plug-in follows the host’s play, stop and time position. Studio reads tempo changes from the OSTF score. To line up bars with other tracks, match the host tempo map to the song. This demo starts at **78 BPM in 4/4**.

## Assign a scale to the project

In the development build, load **OSTF Scale Editor** on an instrument track **inside OSTF Studio**. In another DAW, load **OSTF Studio (VST3)** first and open the project there. Choose a chord in the left pane, audition candidates, then choose **Set scale**. The check mark and scale name show the assignment for the selected target. **Harmony** updates Studio’s global scale lane. A pitched track receives its own scale direction. Save the project in Studio or in the DAW containing it.

If you choose the DAW installer preset, also select **OSTF Scale Editor** under **Composition**.

## Start in standalone Studio

Open the Studio application, choose your audio output under **Settings**, then choose a project under **File → Demo songs** and press Play. To add devices, scan VST3s in the plug-in browser and drag an instrument or effect to the corresponding mixer slot.

### Arrangement and mixer

The section, chord, key and scale lanes apply across tracks. Select a MIDI clip to open the piano roll below the arrangement. Drag a note to change its pitch or start time. On an audio clip, double-click the waveform to add a warp marker and drag it horizontally to adjust timing. Drag a note in the pitch lane vertically to transpose that interval of monophonic audio.

![Arrangement view with chord regions above the piano roll](../images/arrangement-piano.png)

Use **M** to mute a mixer strip and **S** to solo it. To play a track from a keyboard, select its **IN** source and arm it with **R**. The illustrated guide lists the instruments you can put on a track and the effects you can insert after them. [Browse the plug-ins](../plugins/en.md)

### Compare takes in Live Session

Rows are song sections and columns are tracks. Pick MIDI takes for a section, set its play count and add the combination to the queue. Performers can follow the chord display above the grid. Adopting a take updates the arrangement for the same song.

![Live Session with chord display, section queue, track grid and piano roll](../images/ostf-studio-live-session.png)

### Read and print a part

Use the lead sheet to read chords and lyrics, or select a track in the score view to read its part. Open **Print preview**, choose the paper, margins, page breaks and stored credits to show, then select **Save PDF**.

![Paper Lantern Replies part with chord symbols and notes on a grand staff](../images/score-view.png)

## Choose a save format

| What you want to keep | Where to save |
| --- | --- |
| The session in your usual DAW | Save the **host DAW project**. It stores Studio’s song and device state with the plug-in instance. |
| A Studio production to reopen or move | Save an **OSTFP project** from Studio. The destination computer also needs the VST3s and any external sound libraries used by the project. |
| The musical score to exchange with another tool | Save an **OSTF score** in Studio. In the development build this includes the scales you assigned in Scale Editor. |

## If you hear silence

- **Studio is missing from the instrument browser.** Check that your build includes **OSTF Studio (VST3)**. Windows Setup installs it under `C:\Program Files\Common Files\VST3\OSTF Studio`. Enable the system VST3 folder in the host and rescan.
- **The song position does not move.** Return the DAW to the song start and check **Follow DAW transport** and the host Play button.
- **The timeline moves but the track is silent.** Check instrument assignments in Studio, then the Studio master meter and the host track’s stereo output in order. Install the demo’s instruments and effects on the same computer.

## Export audio and bounce a track (development build)

Choose **File → Export mix as audio** to render the arrangement from its beginning. Choose a format, sample rate and bit depth or encoding rate. **Effect tail after song end** keeps up to 30 seconds of reverb and delay. The stereo file includes track inserts, buses, returns and the master. Record an external MIDI instrument to an audio track before exporting its sound.

WAV supports 16/24-bit PCM and 32-bit float. AIFF and FLAC offer 16/24-bit PCM. Ogg Vorbis and Opus offer encoding quality choices. Windows uses Media Foundation for MP3 and AAC (.m4a). macOS uses the system AAC converter and offers MP3 when LAME is installed in a standard Homebrew location.

Right-click a source track and choose **Bounce to new audio track**. The new track contains the selected instrument or audio clips and its insert effects, at 48 kHz/24-bit. To audition it, mute the original and unmute the bounce. For a shared multichannel instrument, mute the original MIDI part as appropriate for your routing. Save as **OSTFP** to keep the rendered audio. The new strip starts at unity and routes to the master. Bus and return processing can be reassigned in the mixer.

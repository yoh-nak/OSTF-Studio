# OSTF Studio user manual

[日本語](README.md) · [Illustrated plug-in guide](../plugins/en.md) · [Project home](../../README.md)

These screenshots show the Windows interface in Public Alpha 2. The installer includes the standalone workstation and 41 VST3s. On macOS, choose the disk image for your processor and run the package inside it.

<a name="studio-vst3"></a>

## Play a song inside your usual DAW

If you work in Ableton Live, Cubase or another VST3 host, load **OSTF Studio** as an instrument on a track in that DAW. You can edit the score inside Studio and record or mix the rest of the production in your host.

1. **Install the workstation instrument.** In the Windows Public Alpha 2 installer, all components start selected. Choose **Studio VST3 for another DAW** for the workstation and the instruments and effects used in the demo. The individual workstation checkbox is **OSTF Studio (VST3)**.
2. **Create an instrument track.** Rescan VST3s in your host and search for **OSTF Studio** in its instrument browser. In Ableton Live, drag it from the browser to a MIDI track. In Cubase, open **Add Track → Instrument** and select it as the instrument. [Ableton VST3 setup](https://help.ableton.com/hc/en-us/articles/209071729-Using-VST-plug-ins-on-Windows) · [Cubase instrument tracks](https://www.steinberg.help/r/cubase-pro/16.0/en/cubase_nuendo/topics/tracks_about/tracks_about_add_track_instrument_settings_r.html)
3. **Open the song inside the plug-in.** Choose **File → Demo songs → Paper Lantern Replies**, or open your own `.ostfp` project or `.ostf` score.
4. **Start your DAW at the beginning.** Enable **Follow DAW transport** at the top left of the plug-in. Studio sends a stereo mix to the host track. The lead vocal enters at bar 5. To audition with Studio’s own transport, turn follow off.

![Hosted workstation with the DAW transport follow switch above the Studio transport, arrangement and piano roll](../images/plugins/ostf-studio.png)

The plug-in follows the host’s play, stop and time position. Studio reads tempo changes from the OSTF score. To line up bars with other tracks, match the host tempo map to the song. This demo starts at **78 BPM in 4/4**.

<a name="scale-assignment"></a>

## Assign a scale to the project

In Public Alpha 2, load **OSTF Scale Editor** on an instrument track **inside OSTF Studio**. In another DAW, load **OSTF Studio (VST3)** first and open the project there. Choose a chord in the left pane, audition candidates, then choose **Set scale**. The check mark and scale name show the assignment for the selected target. **Harmony** updates Studio’s global scale lane. A pitched track receives its own scale direction. Save the project in Studio or in the DAW containing it.

If you choose the DAW installer preset, also select **OSTF Scale Editor** under **Composition**.

Choose the section in the header, then click **Play section**, or use **Play song** to start from the beginning. The header shows the sounding scale. A completely unassigned range plays suggested scales. A gap between saved assignments stays silent. To check the audio route, select the reference tone or an instrument in the header and play a test note.

![Scale Editor showing the Neon Platforms chord list, comparison cards and audition controls](../images/plugins/ostf-scale-editor.png)

## Start in standalone Studio

Open the Studio application, choose your audio output under **Settings**, then choose a project under **File → Demo songs** and press Play. To add devices, scan VST3s in the plug-in browser and drag an instrument or effect to the corresponding mixer slot.

### Arrangement and mixer

The section, chord, key and scale lanes apply across tracks. Select a MIDI clip to open the piano roll below the arrangement. Drag a note to change its pitch or start time. For audio clips, the lower editor shows the waveform and detected pitches.

![Arrangement view with chord regions above the piano roll](../images/arrangement-piano.png)

Use **M** to mute a mixer strip and **S** to solo it. To play a track from a keyboard, select its **IN** source and arm it with **R**. The illustrated guide lists the instruments you can put on a track and the effects you can insert after them. [Browse the plug-ins](../plugins/en.md)

### Edit audio timing and pitch

On an audio clip, double-click the upper waveform to add a warp marker and drag sideways to change its timing. Select a detected note below the waveform, then drag vertically or use the semitone buttons. **Reset pitch** restores that note. Pitch edits apply to the selected clip. **Link aligned stems** shares timing edits with matching clips.

<a name="timeline-edit"></a>

### Enter a chord or scale

Click the region you want in the **Chord** lane. The lower editor opens on that chord. Choose its root, quality and optional bass note. Click the **Scale** lane at the same position to edit the scale instead. Choose a tonic and preset, then apply it. When a saved scale spans several chords, use **This chord only** to change one chord while preserving the rest.

![Chord lane editor showing the selected Am9 chord, its root, quality and bass selectors](../images/chord-lane.png)

![Scale lane editor with tonic, preset and semitone intervals ready to apply](../images/scale-lane.png)

![Global timeline with separate Chord, Key and Scale rows, including an unassigned scale interval](../images/global-scale.png)

| Lane | Where a shared boundary snaps |
| --- | --- |
| Section and meter | Bar line |
| Key | Beat |
| Chord and scale | Half beat |

Split a region with the scissors, turn the scissors off, then drag the shared edge left or right. The adjoining regions resize together. To add time, right-click a global lane and choose the number of bars to insert before the clicked bar. Split a repeating MIDI clip first if the insertion falls inside it. Bar insertion is restricted for scores with authored notation or musical time maps.

Select an arrangement clip and press **Ctrl+A** (**⌘A** on Mac) to select all clips. To move a selection together, right-click it and choose the command to move it to the cursor. Relative spacing is preserved, including the takes and comp ranges of a selected take group.

<a name="drum-loop"></a>

### Set a drum loop on the ruler

In the drum step editor, drag across the bar ruler to mark the range. Drag the left handle to change the first bar or the right handle to change the last. Check **Loop** and the displayed range. Zoom out in the piano roll to see more bars on the shared time axis.

![Step editor bar ruler and loop handles above kick and snare hits](../images/drum-steps.png)

### Compare takes in Live Session

Rows are song sections and columns are tracks. Pick MIDI takes for a section, set its play count and add the combination to the queue. Performers can follow the chord display above the grid. Adopting a take updates the arrangement for the same song.

![Live Session with chord display, section queue, track grid and piano roll](../images/ostf-studio-live-session.png)

<a name="score-piano"></a>

### Read and print a part

Use the lead sheet to read chords and lyrics, or select a track in the score view to read its part. Open **Print preview**, choose the paper, margins, page breaks and stored credits to show, then select **Save PDF**.

![Paper Lantern Replies part showing bar numbers, chord symbols and staff notation](../images/score-view.png)

Open **Score** and click **Piano Roll** in the top toolbar. Picking a part in the score updates the editor below it. Move or draw a note in the piano roll to update the staff. Click **Piano Roll** again for a full-height score.

![The same part open in the score above and the editable piano roll below](../images/score-piano.png)

## Choose a save format

| What you want to keep | Where to save |
| --- | --- |
| The session in your usual DAW | Save the **host DAW project**. It stores Studio’s song and device state with the plug-in instance. |
| A Studio production to reopen or move | Save an **OSTFP project** from Studio. The destination computer also needs the VST3s and any external sound libraries used by the project. |
| The musical score to exchange with another tool | Save an **OSTF score** in Studio. In Public Alpha 2 this includes the scales you assigned in Scale Editor. |

## If you hear silence

- **Studio is missing from the instrument browser.** Check that your build includes **OSTF Studio (VST3)**. Windows Setup installs it under `C:\Program Files\Common Files\VST3\OSTF Studio`. Enable the system VST3 folder in the host and rescan.
- **The song position does not move.** Return the DAW to the song start and check **Follow DAW transport** and the host Play button.
- **The timeline moves but the track is silent.** Check instrument assignments in Studio, then the Studio master meter and the host track’s stereo output in order. Install the demo’s instruments and effects on the same computer.

<a name="audio-export"></a>

## Export audio and bounce a track (Public Alpha 2)

Choose **File → Export mix as audio** to render the arrangement from its beginning. Choose a format, sample rate and bit depth or encoding rate. **Effect tail after song end** keeps up to 30 seconds of reverb and delay. The stereo file includes track inserts, buses, returns and the master. Record an external MIDI instrument to an audio track before exporting its sound.

WAV supports 16/24-bit PCM and 32-bit float. AIFF and FLAC offer 16/24-bit PCM. Ogg Vorbis and Opus offer encoding quality choices. Windows uses Media Foundation for MP3 and AAC (.m4a). macOS uses the system AAC converter and offers MP3 when LAME is installed in a standard Homebrew location.

Click a track name, then Ctrl-click (Command-click on macOS) other track names. Shift-click selects a contiguous range. Right-click a selected header and choose **Bounce tracks to one audio track**. The number beside the command is the number of source tracks. The piano roll keeps its editing focus while you extend the selection. A single source prints its instrument and inserts before its fader. Multiple sources are summed with their strip levels and pan before bus and master processing.

To render an excerpt, select the clip in the arrangement and right-click it. Ctrl/Cmd-click additional clips to select passages across tracks. The new audio starts at the earliest selected clip and ends at the last selected clip boundary. Gaps keep their original duration. The new 48 kHz, 24-bit track starts muted. Save the project as OSTFP to keep its audio.

## Mixer, song information and AI prompts

Open **Mixer** to see the track instruments, inserts, sends, output routing and master. Set the input and arm each track you want to play or record.

![Studio mixer with the track faders, sends and output meters](../images/mixer.png)

Open **Details** beside the song information strip to edit the song and its credits. Print preview lets you select which of the stored credits to display.

![Song information and credit settings](../images/song-settings.png)

Use the **Mode** menu to choose arrangement, lyric or vocal work. The prompt guide beside the timeline changes with the mode. Use its arrows to read another example. The toolbar shows the ChatGPT connection status. When working with an external AI, choose the OSTF material to share and review the result before importing it.

![AI Arrangement Studio prompt example and navigation arrows](../images/ai-prompt-guide.png)

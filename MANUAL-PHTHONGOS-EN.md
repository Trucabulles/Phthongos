# Phthongos User Manual

Working version: 22 October 2026 · Native Windows C++ / JUCE application · English interface.

This manual explains how to transcribe, check and edit MIDI in this version of Phthongos. Other experimental branches may use different engines. The `MANUAL-EN.md` guide and the **?** button provide quick references.

## Contents

1. [Getting to know Phthongos](#1-getting-to-know-phthongos)
2. [Creating opening and saving a project](#2-creating-opening-and-saving-a-project)
3. [Importing and transcribing audio](#3-importing-and-transcribing-audio)
4. [Reading the spectrogram](#4-reading-the-spectrogram)
5. [Working with voices and layers](#5-working-with-voices-and-layers)
6. [Editing notes](#6-editing-notes)
7. [Comparing and adding material with layers](#7-comparing-and-adding-material-with-layers)
8. [Musical grid and BPM](#8-musical-grid-and-bpm)
9. [Playback and navigation](#9-playback-and-navigation)
10. [Exporting](#10-exporting)
11. [Working methods](#11-working-methods)
12. [Quick reference](#12-quick-reference)
13. [MIDI input and tempo estimation](#13-midi-input-and-tempo-estimation)
14. [Metronome and MIDI count-in](#14-metronome-and-midi-count-in)
15. [Overlaid and stacked voices](#15-overlaid-and-stacked-voices)

## 1. Getting to know Phthongos

Phthongos lets you transcribe, check and edit MIDI against an audio recording. A transcription provides a first draft. Overlaying the notes, listening and making corrections lets you complete or arrange it.

| Element | Purpose |
| --- | --- |
| **Audio** | Reference sound recording. |
| **Spectrogram** | A view of the signal over time, frequency and energy. |
| **MIDI** | Editable notes with pitch, start, end, velocity and instrument. |
| **Voices** | Independent musical layers for organising notes. |
| **Grid** | A time reference in musical units or seconds. |

The piano roll places time horizontally and pitch vertically. The three views, **MIDI**, **Audio + MIDI** and **Overlay**, share time navigation.

MIDI corrections do not rewrite the source WAV. A layer lets you preserve a reference while working in another voice. **Ctrl+Z** undoes editing operations; saved versions preserve stages of your work beyond the current session.

## 2. Creating opening and saving a project

### Starting a session

The **☰** menu contains file operations.

- **New project** returns to an empty session in the same window. If there are unsaved changes, the application offers to save them.
- **Open audio** loads a source for transcription or spectrogram work.
- **Import MIDI…** opens an editing project, with or without associated audio.
- **Add MIDI to project…** adds material to the current project.
- **Open project…** restores a `.pht` file.

Drag one file at a time from Windows Explorer into Phthongos. Dropping MIDI opens the add-to-project workflow with voice mapping when MIDI notes are present; otherwise, it opens the import workflow. Dropping audio onto a MIDI project associates or replaces its reference audio, preserving notes, voices and corrections. The usual formats and limits apply.


A PHT project stores the editing state, voices, their colours and locks, the last 25 undo states and working settings. It also stores the audio and analysis data needed to restore the project. Exporting MIDI or WAV does not replace this project save.

The project initially takes the imported MIDI or audio name. Right-click the name at the top, or choose **☰ → Rename project…**, to change it. The name is stored in the `.pht` and used for MIDI exports. Existing files keep their filenames.

### Saving and changing folders

**Ctrl+S** and **Save** save the project. On the first save, choose a name and location. **Ctrl+Shift+S** opens **Save as…**. After a successful save, the chosen name and folder become the project's references.

In **Settings**, **Default project folder (optional)** suggests a location for new projects. You can leave it empty. Each project subsequently uses its last saved location; changing the default folder does not move existing projects.

### Incrementing saves

The **Increment saves** option is off by default. When enabled, every **Ctrl+S / Save** creates a new file:

```text
leaf_2026_10_20_115132.pht
leaf_2026_10_20_115132_02.pht
leaf_2026_10_20_115205.pht
```

The base name remains `leaf`. Dates do not accumulate. The suffixes `_02` and `_03` distinguish files saved within the same second. The new version becomes the current project and earlier files remain intact.

**Save as…** respects the name you explicitly choose. Subsequent incremented saves use this new base name and folder. With the option off, Ctrl+S overwrites the current file as usual, even if its name already contains a date.

### Recent projects

**☰ → Recent projects** keeps ten distinct .pht files, sorted by modification date with the most recent first. Each saved version has its own entry showing its actual filename, modification date and time, and folder. The same path appears only once. A file that can no longer be found is disabled in the list.

**Clear recent projects** clears only this list. The files remain on disk. Opening a recent project also offers to save any changes in the current session.

## 3. Importing and transcribing audio

1. Click **Open audio** and choose a file, or drop audio from Windows into a session without MIDI notes.
2. **Basic Pitch** is included, selected by default, and runs offline on CPU.
3. Choose one entry in the single **Engine** list: Basic Pitch or a configured MuScriptor variant.
4. Click **Analyse**.
5. Listen to the result and examine it in the piano roll.


The eight Basic Pitch settings occupy a full-width band between the engine and transport, in two compact rows. The “Basic Pitch settings” button folds the band without disabling its settings, even during calculation; its state is remembered. “Reset” restores all eight initial values. Before the first analysis, they configure the upcoming calculation. Afterwards, adjusting a control updates the notes after a short pause without running the neural model again.

| Control | Effect |
| --- | --- |
| Note sensitivity | Higher retains more notes; lower removes more weak detections. Initial value: 70%. |
| Attack separation | Higher distinguishes more repeated attacks; lower favours longer notes. Initial value: 50%. |
| Minimum duration | Discard shorter fragments. Initial value: 125 ms. |
| Gap tolerance | 1 to 25 frames; initially 11 (about 128 ms). Tolerate brief activation dips during reconstruction. Hover for approximate duration. Adjustable after calculation without new inference, including after reopening the project. |
| Minimum / maximum note | Limit the detection range, including both endpoints. Initially A0 to C8 (MIDI 21 to 108). Each endpoint is a separate setting. |
| Additional attacks | Infer attacks from activation changes; initially enabled. |
| Additional note recovery | Reconstruct notes from remaining activations; initially enabled. |

The range and both switches also reconstruct notes interactively from complete outputs; widening the range restores previously excluded detections. All eight settings belong to each analysis and return when its voice is selected.

Neural outputs are stored with settings and notes in the `.pht`. Controls reuse the selected voice’s cache, including after reopening and while another stem is the audio reference. When several voices have Basic Pitch caches, select one voice before adjusting its notes. Elapsed time and percentage appear at the top only during calculation; the final duration remains in the bottom status line. Confirmation protects corrections in the selected voice; other voices stay intact. Changing engines never starts analysis.

To add MuScriptor, enable **Use third-party engines** in **Settings**, then select or explicitly download the Small, Medium or Large model files. The **Automatic / CPU / GPU** setting applies to the whole MuScriptor family, regardless of how many models are installed. The **Engine** list shows only installed variants. Disabling third-party engines returns to Basic Pitch without deleting files or declarations. Each file is checked by SHA-256 fingerprint; no download starts automatically. MuScriptor weights by Kyutai and Mirelo are CC BY-NC 4.0 (non-commercial use); they are downloaded separately and are not included in the installer.

Basic Pitch works best on an isolated instrument source. Its initial velocities come from model activations; **Dynamics from audio** can then use the stem’s harmonic energy to propose velocities for listening and explicit application.

The models produce proposed note events. Notes may be missing, fragmented, repeated or mistaken for harmonics. The result needs musical and acoustic checking.

Importing MIDI preserves its notes. With reference audio associated, **Analyse** adds transcription in a new voice and remains available after analysis. If MIDI notes are already present, **Open audio** or dropping another WAV replaces only the audio reference; **Analyse** then adds a new voice without overwriting existing voices. One **Ctrl+Z** undoes the entire addition. **New project** starts an independent session.

When adding MIDI, the preview shows existing notes and the notes to import. For each source part, choose an existing voice or a new voice. An existing destination determines the instrument used. The offset is in seconds; zero preserves the imported time positions. One Ctrl+Z undoes the entire addition.

You can also add your own **SF2** files in **Settings → Personal SF2 banks**. In a voice’s **Instrument…** dialog, choose the bank and one of its actual presets; the bank does not need to follow General MIDI. Playback, audition, MIDI keyboard monitoring and WAV export use that choice, preserving the voice name and notes. The project remembers the file path, fingerprint and preset. Personal banks are read in place and are not embedded in the project: keep the file available. Removing a bank from the catalogue neither deletes its file nor changes configured voices. MIDI export does not include SF2 sounds.

## 4. Reading the spectrogram

Choose **Overlay**, then **Spectrogram**. The signal appears behind the MIDI rectangles at the same time and pitch coordinates.

**Opacity** controls the visibility of the background. **Visual gain**, the palette and contrast affect its readability. They do not change the audio or notes. The high contrast **Inferno** palette can help you examine bright energy centres.

### Understanding the shapes

An attack often appears as a brief increase in energy or a vertical structure. A stable pitch can form a horizontal line. Several lines regularly spaced in frequency may be harmonics of the same sound. After an attack, fading energy can continue because of resonance, sustain pedal or reverb.

Yellow areas in Inferno indicate high energy under the current display settings. They do not, on their own, measure MIDI velocity or played duration. Timbre, register, harmonics and contrast also affect the display.

A visible line can still be a useful starting point for deliberately adding a note, doubling a melody or enriching a chord. Phthongos lets you make that arrangement decision independently of an analysis probability.

### Comparing a short passage

Zoom into a short passage and listen to the audio, then the MIDI. Check attacks, repeated attacks at the same pitch and note endings. Corrected MIDI becomes a useful reference for identifying what remains unexplained in the signal.

**Spectrum / register…** limits the pitches examined by certain analyses. It does not filter the sound. **Mask / guided MIDI** lets you paint areas to examine and obtain note proposals from the signal without running a new neural transcription.

The mask brush and eraser use a circular footprint. Proposals are divided into categories, including **Likely** and **To review**. Listen before adding them: the categories are clues. Tracking a dominant line can miss notes within chords.

## 5. Working with voices and layers

The voice selector determines the active part. **All parts** gives an overall view. For precise corrections, choose a voice and use the others as context.

### Each voice’s source audio

Transcribed voices show **Source: stem-name.wav**, with a small **↺** button. It reloads the file into the single audio reference area without changing notes or starting analysis. In overlaid view the line belongs to the selected voice; each stacked lane has its own line. The path is saved in the `.pht`. If the file was moved, the button lets you locate it. Voices with unknown origins are not assigned a guessed source.

### Creating an empty voice

Click **+** beside the selector. Choose:

- a voice name, such as `Additions`, `Variant` or `Reference`;
- its instrument;
- one of the palette's **12 colours**.

The new voice becomes active. It can receive drawn, pasted or imported notes through **Add MIDI to project…**. Creating it does not transfer or delete existing notes. At startup or after **New project**, an empty Piano voice lets you draw notes and audition them on the keyboard without importing a file. The sound bank is prepared at startup; a message indicates when the project is ready.

Palette swatches display no text; hover over them to see their names. An outline marks the chosen colour. The coloured swatch button beside the selector changes an existing voice's colour. Colour is a project setting and does not change MIDI events or sound.

The **Instrument…** button changes the active voice's sound, including when the voice is empty and no notes are selected. Its custom name is preserved. Changing the instrument through the right-click menu follows the same rule. Colour, positions and durations are preserved; Ctrl+Z undoes the change.

### Visibility sound and protection

| Control | Effect |
| --- | --- |
| **Eye** | Show or hide the active voice. With All parts selected, show or hide all voices. Each voice keeps its visibility in the project without changing playback. |
| **Speaker** | Mute or unmute the chosen voice. |
| **Voice volume** | Set its listening level from 0 to 100% without changing velocities. Double-click resets it to 100%. |
| **Padlock** | Protect its notes against editing and display them in grey. |
| **Colour** | Recolour an unlocked voice. |
| **Instrument…** | Change its instrument while preserving its name. |
| **Trash** | Delete the voice and all its notes after confirmation. |

Changing the active voice does not change which voices are audible: all unmuted voices remain audible. Per-voice volume is stored in the project. It affects listening without changing MIDI or WAV exports.

The horizontal keyboard at the bottom has a fixed compact height and is used for auditioning. Hold a key to play and release it to stop. Drag between keys to explore pitches. It uses the active voice's instrument and volume, respects global MIDI volume and works on a locked voice. It creates no notes.

During a take, project changes, voice changes and editing are disabled. Closing the window first ends the take, then offers the usual save prompt. A device disconnection also ends the take and preserves the notes received. Step input is disarmed on disconnection and when loading a project. Reconnection restores auditioning without starting a take. Playing is never automatically quantised.

Locking does not mute a voice. Muting does not delete notes. Unmuting a voice does not unmute notes that were muted individually.

Deleting a voice includes notes that are muted or hidden by the detection threshold. The confirmation shows its name and total note count. Ctrl+Z restores the voice in one operation. A locked voice remains protected.

### Hatching and status colours

Hatching marks **same-pitch overlaps** over their shared duration, including across voices. It does not indicate a lock. Right-click a hatched area to access the overlapping notes; the diagnostic of the same name helps locate them.

Bright magenta marks **out-of-scale** notes. Other colours indicate playback, diagnostics or locking. These indications take priority over a voice's chosen colour. An out-of-scale note may be entirely intentional.

## 6. Editing notes

Click a note to select and audition it. **Ctrl+click** adds or removes a note from the selection. Drag through empty space to select the rectangles crossed; **Shift+drag in empty space** selects a time range.

| Gesture | Result |
| --- | --- |
| Pencil or double-click in empty space | Add a 500 ms note, then adjust it as needed. |
| Drag a note's body | Move the note or selected group. |
| Shift+drag a note | Move along one axis, chosen at the start of the gesture. |
| Left / right edge | Adjust the start / end. |
| Top / bottom edge when sufficiently zoomed in | Adjust velocity. |
| Ctrl+drag a note | Duplicate freely. |
| Ctrl+Shift+drag a note | Duplicate along one axis. |
| Alt+click | Delete the touched note. |
| Delete | Delete the selection. |

For **Shift+drag** and **Ctrl+Shift+drag**, a sufficiently clear direction selects the axis; small initial diagonal movements are ignored. The axis remains locked until mouse release, even if you release Shift earlier. Vertical gestures preserve exact start and end times; horizontal gestures preserve pitches. A gesture that remains ambiguous creates no copies and does not change the selection.

Duplication preserves the originals and selects the copies. Dragging a selected note duplicates the editable group; an unselected note is copied alone. Ctrl+click without dragging creates no copy. **Escape** cancels the gesture before it is committed.

### Duration and velocity are separate settings

The rectangle's full width represents MIDI duration. The filled proportion represents velocity, from 1 to 127. **The transparent portion is not a resonance tail.** Moving the boundary changes the note's intensity, not its release time.

Hovering shows the start, end, duration in milliseconds and velocity. Sound can continue after the MIDI ending depending on the instrument, reverb or sustain pedal. Acoustic estimates of the useful ending do not establish the exact moment a key was released.

### Splitting merging and linking

- Use the **scissors** button, or **hold C and click**, to split at the chosen point. Each fragment must last at least 10 ms.
- **F** merges compatible selected notes of the same pitch and voice.
- **L** links selected notes with an adjustable overlap while preserving subsequent attacks. The right-click menu also lets you remove a link.
- Right-click to access detailed edits and the available note-extension actions.

The **note eraser** deletes rectangles it touches in the active part. Its mouse wheel adjusts the diameter; one Ctrl+Z undoes an entire stroke. The mask eraser only erases the analysis mask.

## 7. Comparing and adding material with layers

### Applying spectral proposals

Comparison can search for notes in audio with an empty MIDI layer, or complement existing MIDI. It examines MIDI 0–127 (C−1 to G9), limited by the chosen register. Longer windows improve low-pitch separation while short windows locate attacks. Transients provide additional evidence without establishing a pitch on their own. Detection remains experimental; the full range does not guarantee uniform reliability, particularly at the extremes.

In **Check MIDI against audio**, expand a category and use its batch command to add all proposed missing notes, merge sustained notes, apply durations / reattacks or delete possibly extra notes. **Apply all proposals…** combines the categories from the latest verification, within the analysed area.

Lists display 50 rows per page without cutting results at 256. Batch commands include every page. The summary lets you choose a destination or **New voice…**, even from a WAV without MIDI. Locked voices and conflicts are excluded. Listen before confirming; one **Ctrl+Z** undoes both voice creation and the notes. Run verification again after applying; compatible spectral data is reused.

### Copying to another voice

1. Choose the source voice and select the notes.
2. Press **Ctrl+C**.
3. Choose the destination voice.
4. Press **Ctrl+V**.

If the destination is another voice, copies keep their original times and pitches even if the playhead is elsewhere. They adopt the destination instrument. Durations, spacing, velocities and individual mutes are preserved.

In the **same voice**, pasting places the first attack at the playhead. With **All parts**, it preserves the original voices and places the group at the playhead. The clipboard is internal to Phthongos. Ctrl+Z undoes the entire paste.

### Testing a variant

Create a `Variant` voice, copy a passage into it, then lock the reference. Edit only the variant. Alternate mutes to compare them. If you want to hear them separately, avoid playing the reference and its copy together unintentionally.

To add material, create an `Additions` voice, choose its colour, then draw, paste or import the extra notes. The original transcription remains intact as long as you do not edit it.

## 8. Musical grid and BPM

The **BPM** button to the right of the grid selectors opens **Tempo / rhythm calculator**. BPM is a musical reference; changing it preserves attacks and actual durations in seconds.

A quarter note lasts `60,000 / BPM` milliseconds.

| Note value | 65 BPM | 120 BPM |
| --- | ---: | ---: |
| Whole note | 3,692.308 ms | 2,000 ms |
| Half note | 1,846.154 ms | 1,000 ms |
| Quarter note | 923.077 ms | 500 ms |
| Eighth note | 461.538 ms | 250 ms |
| Sixteenth note | 230.769 ms | 125 ms |

The calculator also compares an entered duration with straight, dotted and triplet values. The result is an indication; it does not rewrite the note.

Three modes are available: **Musical**, **Seconds** and **None**. Musical grid values include `1/4`, `1/8`, `1/16`, `1/8T` and `1/16T`. The `T` indicates a triplet. The seconds grid supports time measurements and comparison with audio; its density adapts to the zoom level.

**The current grid is visual and does not quantise notes.** Displaying 1/8 does not move attacks or change durations. Small timing variations in the performance remain intact.

Imported MIDI may contain several tempos. The setting lets you preserve that tempo map or set a fixed reference BPM for export. A WAV does not automatically provide a reliable BPM; enter the piece's tempo when you know it.

A suitable BPM helps conversion to notation. It does not guarantee simple notation: durations, attacks, overlaps and the notation software's import rules also matter.

In **Tempo / rhythm calculator**, **Estimate from notes** proposes a tempo from the current notes after transcription or MIDI import and after corrections. Select a voice in the voice list to analyse it, or **All parts** for the complete set. Individually muted notes are excluded; voice visibility and playback mute do not change the scope. The result identifies the analysed voice and offers the BPM, half, double and other candidates. Choose a proposal, audition it with the metronome, then click **Apply** to retain the tempo. Estimation alone and cancellation change neither project BPM nor notes. Run estimation again after changing notes when needed.

## 9. Playback and navigation

**Audio**, **MIDI** and **Audio + MIDI** select the sources you hear. Audio and MIDI volumes are independent. **Space** switches between play and pause. **Follow playback** moves the view during playback.

The supplied piano serves the piano program. The optional General MIDI bank provides other instruments. The status beneath the transport controls gives access to settings; use **Browse** to select an already installed bank. Reverb affects the sound.

In the active piano roll, **← / →** moves the playhead by 0.5 s; **Shift+← / →** uses a 50 ms step. Arrow keys retain their usual function in lists and input fields.

**Ctrl+wheel** zooms time and pitch; **Alt+wheel** zooms pitch; **Shift+wheel** moves horizontally. Use the Hand tool or middle mouse button to pan the view. **Fit all** frames the content.

Right-click the time ruler to add a named marker. Click a marker to move the playhead there; drag it to reposition it. Use markers to find passages you want to check later. They are stored in the project.

## 10. Exporting

### MIDI

**Export MIDI** produces one file per voice, or only the selected voice. A single voice produces one file. Muted voices are excluded; locked voices remain included. Files share the same tempo and time origin. A single voice gives `My project.mid`; multiple voices give `My project - Piano.mid`, `My project - Bass.mid`, etc., for both click and drag exports.

Hold **Export MIDI** and drag to the Desktop, a folder or an application accepting MIDI files. This exports one file per unmuted voice. A preview shows the filename and file count. A normal click opens the destination dialog. Temporary files remain available after dropping for applications that read them later.


MIDI preserves edited events and their time positions; export does not quantise them. Layer colours and diagnostics belong to the Phthongos project and do not affect MIDI sound.

To prepare a score, import the MIDI into your chosen notation or conversion software, then adjust its rhythmic interpretation. Triplets, ties or small rests may result from how that software interprets the timing. This version of Phthongos does not directly export MusicXML scores.

### WAV

**☰ → Export WAV…** renders the audible MIDI notes using the playback instruments. Choose the scope offered in the form. This export renders the MIDI; it does not export the source WAV together with visual overlays.

If a required bank is missing, follow the prompt to install it or select an existing file. Wait for the confirmation that the file has been created. If the result clips, reduce MIDI volume before rendering again.

## 11. Working methods

### Cleaning up a transcription

1. Save a stage of your work and enter the known BPM.
2. Choose a voice and a short passage, then display the overlay.
3. Compare attacks and notes with the audio.
4. Examine overlaps; merge only fragments that belong to the same note.
5. Use the eraser or mute for identified unwanted notes.
6. Set the scale, then use **Select out-of-scale notes**. This command also displays their highlighting. Per-voice detail helps in the overall view.
7. Listen again and save a new stage.

Out-of-scale pitches, short durations and overlaps are clues, not reasons for automatic deletion.

### Estimating dynamics from audio

The **Dynamics from audio** panel estimates relative velocities for notes in the chosen passage or part. **Average level** sets the centre of the proposal; **Contrast (%)**, from 0 to 250%, controls the differences. **Min** and **Max** are integers from 1 to 127; the average level must stay within that range.

Estimation also covers notes drawn by hand, added from the spectrogram, or given a manually edited velocity. **Apply velocities** deliberately replaces values in the chosen scope; locked voices and percussion remain excluded. Pitches and durations are preserved. A later manual edit takes precedence over the audio velocity for that note.

Settings reuse the available measurements. **Recalculate from audio** remains available after the first estimate and forces fresh measurements without changing notes or adding an undo step. Apply velocities afterwards to retain the result.

Estimation alone does not change the MIDI. Listen and compare the proposals, then use **Apply velocities** if they suit the music. Ctrl+Z undoes the application. **Remove all audio dynamics** removes the dynamics corrections from this analysis. These settings and the accepted changes are stored in the project.

### Reconstructing a missing line

Create an `Additions` voice, choose its instrument, paint a limited mask in the spectrogram and examine the guided proposals. Add useful proposals and fill gaps with the pencil. Check same-pitch repetitions and distinguish new attacks from resonance.

### Comparing a variant or enriching an arrangement

Copy the passage to a new voice, lock the reference, then edit the copies. Alternate mutes to compare. To enrich the arrangement, leave both voices audible after checking doubling and velocity balance.

### Combining several MIDI files

Create destination voices or let the import create them. Add each file with a suitable offset, check its start by listening and in the spectrogram, then choose a consistent tempo reference. Importing into an existing voice preserves its instrument. The added file is not changed on disk.

## 12. Quick reference

The following gestures assume that the piano roll is active and no text input field has focus.

| Shortcut / gesture | Function |
| --- | --- |
| Ctrl+S | Save; create a new version when Increment saves is enabled. |
| Ctrl+Shift+S | Save as… |
| Ctrl+Z | Undo, with the last 25 states retained. |
| Ctrl+Y or Ctrl+Shift+Z | Redo. |
| Ctrl+A | Select editable notes. |
| Ctrl+click | Add / remove a note from the selection. |
| Ctrl+C | Copy selected notes. |
| Ctrl+V | Another voice: original coordinates; same voice / All parts: playhead. |
| Ctrl+drag a note | Duplicate freely. |
| Ctrl+Shift+drag a note | Duplicate along one axis. |
| Shift+drag a note | Move along one axis. |
| Shift+drag in empty space | Select a time range. |
| Hold C and click | Split. |
| F | Merge compatible notes. |
| L | Link the selection. |
| M | Mute / unmute the selection. |
| Alt+click | Delete a note. |
| Delete | Delete the selection. |
| Escape | Cancel the current gesture; leave the tool / clear the selection depending on context. |
| R | Start / stop and commit a real-time MIDI take. |
| Space | Play / pause; during a MIDI take, stop and commit it. |
| ← / → | Move back / forward by 0.5 s. With step input active, → inserts held notes and advances one step. |
| Shift+← / → | Move back / forward by 50 ms. |
| Ctrl+wheel | Zoom time and pitch proportionally. |
| Alt+wheel | Zoom pitch. |
| Shift+wheel | Move horizontally. |
| Hand or middle-button drag | Pan the view. |
| Wheel with the note eraser | Adjust its diameter. |
| Double-click the left margin | Return to the beginning without changing zoom. |
| Right-click the timeline | Add / edit a marker. |
| Right-click a hatched area | Access overlapping notes. |

The **+**, **Colour**, **Instrument…**, **Eye**, **Speaker**, **Lock** and **Trash** controls apply to voices. The **?** button opens shortcut help inside the application.

## 13. MIDI input and tempo estimation

**USB / MIDI keyboard.** In Settings, choose **USB / MIDI input**, then save. Notes and chords play the active voice's instrument using their velocity and that voice's volume. The bottom keyboard shows active keys. Auditioning works on a locked voice and creates no notes. MIDI sustain pedal is supported.

**Step input as a secondary tool.** Open **Step input…** to enable this mode and choose **Note length and cursor advance**. Close the panel, hold one or more MIDI keys, then press **→** in the piano roll to insert the held notes and advance by that duration. A key released before the advance creates no note; the others remain available for the next step. There is no Melody / Chords choice or imposed chord detection. Duration follows the local tempo and is independent of the visual grid. Ctrl+Z undoes one step. How long you physically hold the keys does not determine note length in this mode. **R** starts real-time recording directly and disables step input.

**Real-time MIDI recording.** Select an unlocked voice and place the playhead at the desired start, then press **R** or click the **red ●** button near Play and Stop. Playback starts at that position after the count-in, if enabled. Play freely: each Note On displays a note that grows until Note Off. Timestamped attacks, durations, overlaps, chords and velocities are preserved without quantisation, whatever grid is displayed. New notes are auditioned with the active voice's instrument and volume. The pedal affects auditioning; written durations follow the physical keys, without turning sustain into longer rectangles. Click the active record button again (a white square on red), **Stop**, **Pause**, or press **R** or **Space** to finish. Keys still held are closed at the end of the take. Ctrl+Z undoes the entire take; Redo restores it. A take can extend beyond the source audio to the project's 20-minute limit.

Extending the workspace: moving, drag-copying or lengthening notes can go beyond the project's previous end. Holding the pointer near a horizontal edge scrolls the view automatically without changing the time scale. Pasting also extends the project when needed. Escape cancels the drag and its temporary extension. The 20-minute limit still applies.

During real-time recording, the upper indicator shows the take's elapsed duration in minutes and seconds, independently of the playback position in the project. After Stop, Phthongos can propose a stable BPM from the latest take's attacks. The BPM button keeps the project tempo and adds ≈ followed by the pending estimate. Open it to distinguish the project BPM, estimated BPM and accepted BPM. Choose the estimate, half or double it, then apply explicitly, or retain the project BPM. No tempo is imposed and no quantisation occurs: starts, ends, velocities and overlaps remain identical in seconds. A short, sparse or irregular take may produce no estimate. The proposal is indicative and may be ambiguous at half or double tempo; its metadata is stored in the PHT. This analysis neither changes nor groups recorded events.

Post-take estimation uses a native C++ port of MIDI BPM Detection (MIT). Several tempo candidates and their relative scores are retained. These weights compare calculation peaks rather than measure a probability of correctness. The list offers half / double tempo and other hypotheses; confirming a choice does not move or quantise notes. The calculation uses attacks, pitches and velocities without analysing durations or the full musical phrasing. Long or dense takes use bounded analysis windows. No Rust engine or Rust build dependency is shipped.

## 14. Metronome and MIDI count-in

The Metronome button near the transport enables the click during playback and recording. The adjacent ⋯ button sets its volume and count-in: None, 1 bar (initial default) or 2 bars. The count-in follows the tempo and time signature at the starting position and remains audible even if the click is disabled afterwards. The playhead stays in place until recording starts. R, Space or Stop cancels the count-in without inserting notes. Keys played and released before recording starts are not inserted; keys still held at the start begin there with their original strike velocity. Step input does not use a count-in.

In the BPM panel, **Listen to the take with this tempo** plays the MIDI with a temporary click at the proposed BPM. The first click aligns with the first available note, providing a listening reference without proving that the note falls on a downbeat. **Stop tempo audition**, or closing the panel, ends this trial and restores the project click. Notes and the accepted BPM do not change until you apply a choice. The click is never added to notes or MIDI export.

Listening volumes are displayed as percentages: 0% is silence and 100% is the end of the slider's travel. Levels in older projects are preserved. Double-click resets each volume to its initial level. The metronome offers a volume slider and three count-in buttons. The visual beat indicator follows the audio transport and current time signature. It is hidden when the metronome is disabled, except during count-in or a candidate-tempo audition. During count-in, a large numeral appears on the piano roll, counting down again for each bar.

## 15. Overlaid and stacked voices

In MIDI view, **Overlaid / Stacked** changes only the presentation of the same MIDI notes. Audio + MIDI keeps voices grouped with a separate audio area; Overlay keeps the MIDI grouped over the audio signal or spectrogram. Returning to MIDI restores the chosen voice presentation. Stacked view provides one lane per voice, with colour, visibility, mute, volume, instrument and lock controls. Clicking in a lane makes that voice active for editing and auditioning; it does not change which voices are audible. The horizontal keyboard stays at the bottom with a fixed compact height. Only voice lanes and the audio area can be resized. The separate audio area belongs to Audio + MIDI view; no audio space is reserved in MIDI view.

**Fit each voice** initially adapts the register to each voice. **Shared pitch scale** uses the same semitone height and relative framing in all lanes. The ruler, playhead, time zoom and horizontal scrolling remain shared. The wheel scrolls pitches in the lane under the pointer; Shift+wheel moves along time; Alt+wheel zooms pitch; Ctrl+wheel zooms time and pitch. The Hand tool or middle-button drag pans the local register and the shared timeline. Use the wheel over headers or the single outer scrollbar to scroll through voices.

Drag the visible separator between two lanes to redistribute their heights without changing note size. Double-click restores automatic distribution. A lane's arrow folds it without hiding its notes or muting its sound. **Fit voice** adjusts only its register and zoom, without changing its height. Heights, framing and folded states are stored in the project.

To reassign notes, enable **Transfer**, then drag them to the target lane, which highlights before you drop. Alt+Shift+drag performs the same gesture directly. This transfer preserves pitch, attack, duration and velocity. Escape cancels the gesture. One Undo reverses the transfer. Locked voices cannot be the source or destination. Ordinary dragging within a lane retains the usual editing gestures.

On entering Stacked view, lanes share the available height with a compact minimum. The note register is framed without enlarging the lanes. No local scrollbar is displayed; a single outer scrollbar appears when the lanes no longer fit in the window. The bottom keyboard has reserved space and never covers a lane. The shared audio strip disappears in MIDI view. Manual heights and framing are stored in the project.

## Help and licences

**☰ → Settings…** gathers global preferences; the engine and working controls remain in the main interface. **☰ → User manual (PDF)** opens the offline manual matching the interface language in your usual PDF reader.

**☰ → Credits and licences** replaces the previous Information panel. Its dropdown lets you read bundled licence texts offline. It distinguishes integrated components, the bundled CC0 Upright Piano KW, optional MuScriptor weights and the optional MuseScore General HQ bank. Manually declared SF2 banks and models retain their own licences and do not become components distributed by Phthongos.

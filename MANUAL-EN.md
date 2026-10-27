# Phthongos — native Windows application

## Files and projects

Open audio, or drop audio into a session without MIDI notes, for transcription with the bundled Basic Pitch ONNX CPU engine, selected by default and usable offline. The single Engine list also offers configured MuScriptor Small / Medium / Large variants after enabling third-party engines and selecting their model files in Settings. The three inline controls beside the engine adjust note sensitivity, attack separation and minimum duration. After the first analysis, they update notes after a short pause using cached neural outputs for unchanged audio, even after reopening. Before that analysis, they configure the upcoming calculation. Select a voice to adjust its cached notes; confirmation protects its corrections and all other voices remain untouched. With notes already present and reference audio associated, Analyse adds new voices instead of replacing existing MIDI, including imported MIDI. Open audio or dropping another stem changes only the reference audio. Ctrl+Z undoes an entire transcription addition. Each transcribed voice shows its source filename and a ↺ button to reload it as reference without analysis; moved files can be located. Source paths and per-voice caches are saved in the PHT. You can also start editing manually without a model. Import MIDI with or without reference audio. **Add MIDI to project** preserves the current notes and audio, previews the addition, and lets you route every source part to an existing or new voice with an optional offset in seconds. Existing destination voices retain their instrument. The whole import is one undoable operation.

Projects are saved as PHT8. Supported older hybrid projects are opened and converted under a new name, preserving the original. MIDI and WAV exports do not replace source files. Optional MuScriptor weights and the optional HQ bank remain separate from the distribution; Basic Pitch is included.

## Editing and navigation

- Arrow: select by click or rectangle. Ctrl+click adds/removes notes. Shift+drag selects a time range.
- Pencil or double-click empty space: add a 500 ms note. Escape leaves insertion and clears selection.
- Drag a selected note to move the entire selected group; intervals, durations and velocities are preserved. Shift+drag a note constrains movement to one axis. The axis is chosen after a clear initial direction, ignoring small diagonal jitter, and stays locked until mouse release even if Shift is released earlier. Vertical gestures retain exact onsets and ends; horizontal gestures retain pitches.
- Ctrl+drag a note duplicates freely; Ctrl+Shift+drag duplicates along one axis using the same direction threshold and gesture-long lock. An ambiguous diagonal gesture creates no copies and leaves the selection unchanged. Originals remain intact; one Undo removes all copies from the gesture. Ctrl+click without dragging still adds/removes a note from the selection.
- Drag left/right edges to change duration; top/bottom edges change velocity when zoomed in.
- Hold **C** and click a note to split it. Both fragments keep the part, pitch, velocity and mute; minimum 10 ms per fragment.
- **F** merges compatible notes; **L** links selected notes with a configurable overlap; right-click offers Unlink. Linking preserves subsequent attacks and never crosses parts.
- **M** mutes/unmutes notes. Alt+click deletes a note; Delete deletes the selection.
- Ctrl+C copies selected notes internally. Ctrl+V into a different voice preserves original times and pitches. In the same voice (or All parts), paste starts at the playhead. The selected destination receives copies; All parts preserves their original parts.
- The last 25 undo states are retained. Ctrl+Z undoes one complete operation; Ctrl+Y or Ctrl+Shift+Z redoes it.
- Space plays/pauses. Beginning/End buttons move the playhead and view without changing zoom.
- Ctrl+wheel zooms time and pitch together; Alt+wheel zooms pitch; Shift+wheel moves horizontally. Hand or middle-button drag pans.
- **Eraser** deletes touched notes in the active part. Scroll to adjust diameter (8 to 512 px). One stroke is one Ctrl+Z operation; Escape cancels a pending stroke.
- Note details include duration in milliseconds and update while resizing.
- Double-click the left audio/pitch margin to return to the beginning. The **?** button opens shortcuts inside the application frame.

## Views and sound

Choose MIDI, separate Audio + MIDI, or Overlay. Waveform and Spectrogram share time, playhead and zoom. Spectrogram uses a logarithmic pitch axis and needs no model.

Selecting Spectrogram starts with opacity 100% and visual gain 8.0. Its initial palette is Inferno, rendering Subtle, contrast 100. Waveform retains independent settings (initially 20% opacity and gain 4.9). Saved project display settings are respected when reopening. Visual gain changes display only and is available in Overlay.

Spectrum / register offers Inferno, Magma and Viridis. Low/High bounds can be placed with a right-click on the note scale and adjusted by dragging markers or the bar handles. Ranges are stored per part and constrain diagnostic candidates; harmonics outside the range remain context. The missing-note diagnostic examines MIDI 0–127 (C−1 to G9), limited by the chosen register. Analysis at 44.1 kHz uses short windows for attacks and longer windows for low-pitch selectivity. Transients support attack evidence but never establish a note on their own. Proposals still require listening.

The eye shows/hides the active voice without changing playback. Each voice retains its own visibility in the project, including when switching voices. With All parts selected, the eye shows/hides all voices. The speaker mutes the selected part independently of manually muted notes.
Changing the active editing voice never changes which voices are audible. The slider beside the speaker adjusts that voice’s listening level (0–100%; double-click resets to 100%). Each level is saved in the project without changing note velocities or MIDI/WAV exports. Hold a key on the horizontal keyboard at the bottom to audition, release to stop, or slide between keys. Audition uses the active voice instrument and listening level plus the global MIDI volume, works with locked voices, and never creates notes.  Unmuting a part preserves those individual mutes. The trash icon deletes the selected voice and all its notes, including hidden or muted notes, after confirming its name and total note count. One Undo restores everything. Locked voices remain protected.

Piano uses the bundled upright piano; other instruments and percussion use MuseScore General HQ when available. An existing installed bank is reused without downloading it again. Settings offers Download and Browse. No download starts automatically. WAV export requiring General MIDI asks for the bank first.

## Acoustic verification and guided MIDI

Check MIDI against audio proposes missing notes, possibly extra notes, fragmented sustained notes and possible duration/reattack corrections. These are acoustic clues, not certainties or calibrated probabilities. Listen and choose whether to apply any correction. No MIDI changes occur automatically.

For a stem or dominant melodic line, open **Mask / guided MIDI**. Paint a pitch corridor, erase unwanted areas, adjust Low/High, and choose mask extent, visible view or all audio. Alternatively disable **Analyze only the painted selection**. Analyse and propose notes uses PCM spectral/harmonic evidence, including harmonics outside the candidate mask, with no AI model.

An estimated pitch curve and ghost notes appear. Click to locate; Ctrl+click groups proposals. Right-click offers listening, add and ignore actions. Add selected proposals or all explicitly. Additions target the selected part or a new Guided line when All parts is selected. One undo removes a whole accepted batch. Painting/erasing is also one undo per stroke and remains aligned through zoom.

This experimental mode tracks one dominant line, not a full polyphonic orchestra. Octave, onset, duration, reattack and vibrato ambiguities remain possible. Proposed velocities start at 83 and can be edited; they are not estimated from audio. Analysis is cancellable and limited to less than seven minutes per run. Masks and accepted notes persist in PHT8; curves and unaccepted proposals are recalculated.

## Language, saving and installation

Settings offers **Français / English**. The preference takes effect immediately, is stored beside the executable in `data/language.json`, and is independent of PHT projects. User-defined voice names and file paths are never translated. The installer also offers language selection.

When closing a modified project, Save waits for a successful save before closing. A cancelled destination cancels closure. Save failures are shown explicitly; the project stays open and unsaved. Choosing an older project’s original filename proposes a separate PHT8 filename. Playback and transport navigation do not mark the project modified.

For development and build commands, see `BUILD.md` in the source workspace. This distribution is Windows x64; macOS, Linux and VST3 builds are separate work.

### Proposal screening

Harmonic support, explained energy, pitch stability, relative level and temporal context determine three reversible categories: **Likely**, **Review** and **Unlikely**. These are clues, not calibrated probabilities. Unlikely candidates are initially hidden but remain accessible. Add one category, selected proposals or all visible proposals explicitly; MIDI is never corrected automatically.

**Favor the selected scale** is optional and initially off. It applies a small preference only when acoustic evidence is ambiguous, without snapping pitch or excluding convincing chromatic events. Choose the scale before analysing again.

After adding notes, the mask, pitch curve and ghost proposals hide to restore normal editing. Show each independently to continue reviewing: the mask and remaining proposals are retained. The new preferences are stored in PHT8.

## Same-pitch overlaps

Open **Correction / cleanup > Same-pitch overlaps**. This list covers visible parts and requires no audio analysis. Click an entry to locate and select its notes. Hatching marks only their shared portion. Right-click that area to select, audition or delete each note individually, including a completely hidden duplicate. Nothing is deleted automatically; Ctrl+Z restores a deletion.

## Playhead keyboard navigation

With the piano roll focused, left/right arrows seek backward/forward by 0.5 seconds; Shift+arrow uses fine 50 ms steps. The playhead stays visible when zoomed and stops at the track boundaries. Text fields and menus keep their normal arrow-key behavior.

## Guided spectrogram analysis

Run **Analyze and propose notes** again to use harmonic-core energy, local reattacks and fading-resonance screening. Durations and velocities are estimates to verify by ear. Unlikely proposals remain available; no MIDI correction is applied automatically.

Guided analysis V3: independent spectral cores supplement the pitch trajectory, particularly for piano. Proposed durations estimate the useful end before a fading resonance tail; they cannot guarantee the actual key release. Rhythmic values are not inferred without a reliable tempo. Run the analysis again to apply the new rules. The mask brush and mask eraser show a circle matching the selected area at every size.

WAV export uses the same sound banks as playback. Without the General MIDI bank, instruments use the bundled fallback piano; export status explicitly identifies this. A separate timestamped filename is suggested and the created path is displayed. Failures show their actual cause in a dialog and in status. Existing files and sources remain protected.

Shared General MIDI bank: the selected path is saved in %APPDATA%/Phthongos/soundbank-settings.json for new C++ releases. A valid older local preference is migrated when needed. Without a valid saved path, the application also looks for Documents/player-assets/MuseScore_General_HQ.sf2 and verifies its hash before activating it. The bank remains in place without copying or downloading it again. Future downloads use %APPDATA%/Phthongos/soundbanks. Older application binaries retain their previous behavior.

Personal markers: right-click the timeline to add a flag with an optional name. Hover for its name and timestamp; click to seek; drag to move; right-click the flag to rename or delete. Escape cancels an ongoing drag. Markers are independent of notes and grids and saved in PHT8 with Undo/Redo history.
Shift + drag a note or group: the initial gesture chooses the axis. Vertical movement preserves exact timestamps; horizontal movement preserves pitches. Dragging without Shift remains free. Shift + drag in empty space retains time-range selection.
Saving: the stored playhead is bounded by the actual project duration, including notes added after the audio. The five-second editing margin no longer prevents saving. No notes are shortened by this correction.

Drag to duplicate: hold Ctrl for free movement, or Ctrl + Shift to constrain movement to one axis, before grabbing a note. The initial gesture chooses the axis: vertical copies preserve exact start and end times; horizontal copies preserve pitches. Dragging a selected note copies the editable selected group; an unselected note is copied alone. Originals remain in place and visible during preview. On release, the copies become selected. Ctrl+Z undoes the entire duplication and Ctrl+Y restores it; Escape cancels before release. Clicking without movement creates nothing. The clipboard and paste-at-playhead remain available; Shift alone moves along one axis.

## Project tempo and rhythm calculator

Use **Tempo / BPM** beside the grid selectors to declare a tempo
between 10 and 600 BPM, including before transcription. It is saved in PHT8 and
written into MIDI exports. Changing BPM preserves onsets and durations in seconds;
it does not quantize notes or change playback speed. MIDI times are rounded to the
file's tick resolution. For a stem labelled 65 BPM, declare 65 before exporting.

Imported MIDI tempo maps are preserved by default. Fixed mode explicitly replaces
them while preserving note and controller times in seconds. Source mode restores
the original map. Older projects remain compatible.

The calculator converts straight, dotted and triplet note values to milliseconds.
It also finds the nearest value to a duration and shows the signed difference.
It uses the displayed BPM, provides estimates only, and does not modify notes or
automatically detect tempo.

## Visual grid

**Grid** offers Musical (default), Seconds and None. Musical subdivisions are
1/4, 1/8 (default), 1/16, 1/8T and 1/16T. Triplets require an explicit selection.
Bars are stronger than beats, and beats stronger than subdivisions. Zooming out
hides crowded lines without changing the selected subdivision. The musical ruler
shows bar.beat. Seconds mode uses real time, including 500/250 ms subdivisions
when the zoom permits. None hides vertical grid lines but keeps the time ruler.

BPM remains editable at any time. The grid follows the fixed project BPM or the
imported MIDI tempo map in source mode, with imported time signatures. The fallback
meter is 4/4 and the musical origin is zero seconds. Display settings are saved per
project. This feature does not snap, quantize or move notes; manual editing and the
existing Shift / Ctrl+Shift axis constraints remain available.


Voice locks and MIDI export

Select a voice in the parts menu and click Lock. Repeat to lock multiple voices. Locked notes stay visible in gray while editing another voice. Moving, erasing and bulk corrections cannot change them; playback and mute remain available. Locks are saved in the project. Click Unlock to edit again; Undo can also undo a lock.

Export MIDI offers All voices, Selected voice (when a voice is chosen), or One file per voice. The last option creates a new Phthongos-voix subfolder in your chosen parent folder. Files preserve the project tempo, instruments and absolute timing. Muted notes are excluded; locking does not mute a voice. Existing exports are never replaced.


Out-of-scale notes by voice

The Key / scale banner shows the total and its breakdown by voice within the analyzed scope. Selecting a single voice shows its name and its own total. Details by voice lists pitches, times and durations; click a note to locate it. Each voice has its own selection action. Locked notes are counted and remain available for inspection, but are excluded from editing selections. Lists are paginated in batches of 50 notes. The diagnostic never moves or deletes notes.


Out-of-scale workflow and new projects

Configure the scale before selecting out-of-scale notes. Select now analyzes, displays the notes in dedicated bright magenta with a contrasting outline, and selects the editable notes in one click. Display remains available without changing the selection. Locked voices stay gray with a magenta diagnostic outline and are excluded from editing selections.

File > New project starts an empty Piano voice in the same window. Once sound preparation finishes, the bottom keyboard and note drawing work without importing any file. Unsaved changes can be saved, discarded, or kept by cancelling. The tempo and rhythm calculator remain accessible through the BPM button beside the grid.

Save preferences: **Increment saves** is off by default. When enabled, Ctrl+S / Save creates `name_yyyy_mm_dd_hhmmss.pht`; same-second collisions receive `_02`, `_03`, etc. Save As honors the chosen name and folder; the project retains that folder and a stable base name. Without incrementing, Ctrl+S overwrites the current file. The optional default project folder in Settings is used only for new projects. **Recent projects** keeps ten distinct .pht files, sorted by modification time, newest first. Each saved version has its own entry with the actual filename, modification date/time and folder; the same path appears only once. Clearing the list never deletes project files.

The + beside the voice selector creates an empty voice with a name, instrument and one of 12 dedicated colours. It becomes active for drawing/pasting and is available as a MIDI-import destination. The colour button recolours an existing unlocked voice. Diagnostic, playback and lock colours remain reserved. Voice creation and colours support Undo/Redo and PHT persistence.

The 12 colour swatches show no text; names remain in tooltips. Instrument… in the voice toolbar changes the instrument without selecting notes, including for an empty voice. The voice name is retained, including when changing instruments from the context menu.

USB/MIDI keyboard: select the input in Settings and Save. Polyphonic audition uses the active instrument, hardware velocities, voice level and MIDI master level. The bottom keyboard lights held keys; locked voices remain available for audition.

**Step input…** is a secondary tool. Open its panel, enable step input and choose the note length and cursor advance. Close the panel, hold one or more MIDI keys and press **→** in the piano roll to insert the held notes and advance by the chosen duration. Releasing a key without advancing does not insert it. No Melody/Chords selector or chord detection is used. The chosen duration follows the project tempo independently of the visual grid. Undo removes one step. Pressing **R** switches directly to real-time recording.

**Record MIDI:** choose an unlocked voice and start position, then press **R** or click the red ● button beside Play and Stop (Record MIDI in real time tooltip). Playback starts and incoming Note On/Off timestamps define the actual note positions and lengths. Notes appear immediately and grow until release. Chords, overlaps and velocities are retained without quantization, independently of the displayed grid. Sustain controls audition; written note lengths follow physical key release. Click the active recording button again (white square on red), or use R, Stop, Pause or Space to finish the take, closing any held notes. Undo removes the whole take; Redo restores it. Recording can continue beyond the source audio, up to the 20-minute project limit. Project/voice changes and editing are disabled during capture; closing the window finishes the take before the usual save prompt. Unplugging finishes the take and preserves its notes; reconnecting restores audition without restarting recording. Opening a project disarms writing.


Timeline extension: moving, drag-copying and resizing notes can extend past the former project end. Holding the pointer near a horizontal edge scrolls the view automatically, keeping seconds per pixel fixed. Clipboard paste also extends the project as needed. Escape cancels a drag and its provisional extension. The twenty-minute project limit remains in effect.


During real-time recording, the top indicator shows the elapsed take duration (minutes:seconds), independently of the project playhead position. After Stop, Phthongos may propose a stable BPM from the last take’s attacks. The BPM button keeps the current project tempo and shows ≈ followed by the pending estimate. Open it to compare Project BPM, Estimated BPM and Chosen BPM. Choose the estimate, half or double tempo and explicitly apply, or keep the project BPM. No automatic tempo change or quantization takes place: applying BPM preserves all note starts, ends, velocities and overlaps in seconds. A sparse, short or irregular take may produce no estimate. The proposal is indicative and octave-ambiguous, not a musical certainty; its metadata is retained in the PHT project. The analysis does not modify or group the recorded events.

The post-take estimator uses a native C++ port of MIDI BPM Detection (MIT). It retains multiple candidate tempos and their relative histogram scores. These are comparative weights, not probabilities that a tempo is correct. The list includes half/double options and other hypotheses; confirming a choice never moves or quantizes notes. The detector uses onset times, pitch and velocity, not note lengths or a complete phrase interpretation. Long or dense takes use bounded local analysis windows. No Rust runtime or build dependency is shipped.

## Metronome and MIDI count-in

The transport Metronome button enables the click. The adjacent ⋯ button sets its independent volume and the count-in: None, 1 bar (initial default), or 2 bars. The count-in follows the tempo and time signature at the recording start, and is audible even if the click is disabled during the take. R, Space or Stop cancels without creating notes. Notes released before recording starts are ignored; keys still held at that boundary start there with their original strike velocity. Step entry does not use a count-in.

In the BPM panel, Listen to the take with this tempo auditions MIDI with a temporary candidate-tempo click, aligned to the first available note; this does not establish that it is a downbeat. Stop tempo audition or close the panel to restore project click settings. Recorded notes and retained project BPM stay unchanged until a choice is applied. No click notes are added to MIDI exports.

Listening volumes use percentages: 0% is silence and 100% is the right end of each slider; existing project levels are preserved. Double-click restores each volume to its initial level. The metronome settings use a volume slider and three count-in buttons. A beat indicator follows the audio transport and the current time signature. It is hidden when the metronome is off, except during count-in or a candidate-tempo audition. During count-in, a large numeral appears over the piano roll, counting down once per bar.

## Overlaid and stacked voices

In MIDI display, the Overlaid / Stacked selector changes only the presentation of the same MIDI notes. Audio + MIDI keeps grouped voices beside the source audio strip; Overlay keeps grouped MIDI over the waveform or spectrogram. Returning to MIDI restores the chosen voice presentation. Stacked view provides a lane per voice, including colour, visibility, mute, volume, instrument and lock controls. Clicking a lane activates its voice for editing and audition without changing which voices are audible. The horizontal keyboard stays at the bottom with a fixed compact height; only voice lanes and the source-audio area can be resized. The source-audio strip belongs to Audio + MIDI display; no source-audio area is reserved in MIDI display.

Fit each voice initially frames each voice’s own register. Shared pitch scale uses the same semitone height and relative pitch framing across lanes. The ruler, playhead, horizontal zoom and time scrolling remain shared. The wheel scrolls pitches in the hovered lane; Shift+wheel pans time; Alt+wheel zooms pitch; Ctrl+wheel zooms time and pitch. The Hand tool or middle-button drag pans the local register and shared timeline. Scroll over lane headers or use the single outer scrollbar to browse voices.

Drag the visible separator between lanes to redistribute their heights without changing note size; double-click to restore automatic height allocation. The lane arrow folds its editor without hiding notes or muting sound. Fit voice adjusts only its register and pitch zoom, keeping its height. Lane heights, folds and framing are saved in the project.

Enable Transfer, then drag notes onto another lane, which highlights before the drop. Alt+Shift+drag also starts this distinct gesture directly. Transfer preserves pitch, onset, duration and velocity. Escape cancels the gesture; one Undo reverses a completed transfer. Locked voices cannot be the source or destination. Ordinary dragging within a lane retains the existing editing gestures.

On opening Stacked view, lanes share the available height, with a compact minimum. Pitch ranges are fitted inside lanes without growing them. There are no local pitch scrollbars; a single outer scrollbar appears only when the lanes no longer fit. The bottom keyboard has reserved space and never covers the lanes. The common audio strip is hidden in MIDI display. Manual lane heights and pitch framing are retained in the project.


To add MuScriptor, enable **Use third-party engines** in **Settings**, then select or explicitly download the Small, Medium or Large model files. The **Automatic / CPU / GPU** setting applies to the whole MuScriptor family, regardless of how many models are installed. The **Engine** list shows only installed variants. Disabling third-party engines returns to Basic Pitch without deleting files or declarations. Each file is checked by SHA-256 fingerprint; no download starts automatically.

You can also add your own **SF2** files in **Settings → Personal SF2 banks**. In a voice’s **Instrument…** dialog, choose the bank and one of its actual presets; the bank does not need to follow General MIDI. Playback, audition, MIDI keyboard monitoring and WAV export use that choice, preserving the voice name and notes. The project remembers the file path, fingerprint and preset. Personal banks are read in place and are not embedded in the project: keep the file available. Removing a bank from the catalogue neither deletes its file nor changes configured voices. MIDI export does not include SF2 sounds.

Hamburger menu: Settings…, User manual (PDF), Credits and licences. Basic Pitch: seven interactive settings in a full-width collapsible band; range, attacks and recovery reconstruct from each voice’s complete cache. Optional MuScriptor weights are CC BY-NC 4.0 and not bundled.

Basic Pitch: eight settings; gap tolerance from 1 to 25 frames, default 11. Adjustable interactively from each voice’s cache, including after reopening.

BPM → Estimate from notes: active voice or All parts; proposals and metronome audition, then explicit Apply. Available after transcription and MIDI import.

Check MIDI against audio: batch commands in each category and “Apply all proposals…”. Summary, destination voice for additions, locked voices and conflicts excluded; one Ctrl+Z undoes the entire batch. Run verification again after applying.

Spectral comparison retains proposals without the 256-result cutoff; lists show 50 per page and batches include all pages. A WAV alone can be checked and its proposals added through “New voice…”, without AI transcription. Ctrl+Z undoes voice creation and notes together.

Audio dynamics: manual and spectral notes are eligible; explicit application replaces velocities except for locked voices and percussion. Recalculate from audio only renews measurements.

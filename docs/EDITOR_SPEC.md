# Editor behavior specification

## Product interaction model

Videx initially follows the familiar source/program/timeline model used by
professional desktop NLEs. Familiar behavior is more valuable than novelty in
the editing surface. AI augments this model; it does not replace it.

The reference workflow is:

1. import assets into bins;
2. inspect and mark source ranges;
3. insert or overwrite them into a sequence;
4. refine timing in the timeline;
5. adjust properties, audio, transitions, and captions;
6. review, export, or interchange the sequence.

## Default editing workspace

```text
+----------------------+----------------------+----------------------+
| Project / media bin  | Source monitor       | Program monitor      |
| search, bins, assets | source in/out        | sequence playback    |
+----------------------+----------------------+----------------------+
| Effects / history    | Timeline                                    |
| jobs                 | tracks, playhead, ruler, markers            |
|                      |----------------------------------------------|
|                      | Inspector / audio meters                    |
+----------------------+----------------------------------------------+
```

Panels are dockable later, but version 0.1 ships one stable layout before a
general docking system. Every operation must be reachable by keyboard without
requiring hidden hover-only controls.

## Persistent state versus view state

Persistent project state includes assets, sequences, track items, markers,
captions, links, effect parameters, and sequence settings.

View state includes selection, active panel, scroll position, timeline zoom,
expanded track height, visible overlays, and drag previews. View state may be
saved as user workspace data but must not affect rendering or interchange.

The playhead is session state. It is restored for convenience but does not
belong to sequence content or edit history.

## Timeline invariants

- Time is discrete rational time. A sequence edit snaps to video frames; audio
  items may use audio-sample precision when audio-unit mode is enabled.
- A track item has a half-open range `[start, end)`. Adjacent items therefore
  share a boundary without overlapping.
- Ordinary items on the same track cannot overlap. Visual compositing is
  expressed by items on different video tracks.
- Empty track time is implicit. It is not a mutable gap entity.
- Locked tracks cannot be changed by direct or ripple operations.
- A clip references exactly one media stream and one source range. Imported
  video with audio creates separate linked video and audio clips.
- Link groups preserve relative offsets across tracks. The user can temporarily
  override linking for an operation without destroying the link group.
- A transition explicitly references the two sides of a cut and its consumed
  handles. A transition cannot silently read beyond available source media.
- Every committed edit is atomic, revision-checked, and undoable.

## Track controls

Each track has independent controls:

- **lock** prevents all content edits;
- **mute** disables audio contribution;
- **visibility** disables video contribution;
- **sync lock** determines whether ripple operations shift that track;
- **target** determines which sequence tracks receive commands;
- **source patch** maps source video/audio streams to target tracks.

Track targeting and source patching are separate concepts and separate state.
This distinction must remain visible in the UI.

## Core edit semantics

### Insert

Insert places the marked source duration at the playhead on patched target
tracks. Existing content at and after the insertion point moves right by that
duration on affected sync-locked tracks. Sequence markers and captions at or
after the insertion point also shift right by the inserted duration, and a
caption straddling the point is split, so insert followed by extract restores
annotation positions. Locked tracks remain unchanged; the operation is rejected
if that would break a linked edit unless the user chooses an explicit override.

### Overwrite

Overwrite places the marked source range on patched target tracks without
changing sequence duration. Existing item portions under the destination range
are removed or split. Other tracks do not move.

### Lift

Lift removes selected items or a marked sequence range and leaves empty time.
Sequence duration and later item positions do not change.

### Extract / ripple delete

Extract removes a marked range and shifts later items left on sync-locked,
unlocked tracks. Ripple delete applies the same closing behavior to the selected
items or empty range. The preview must show every affected track before commit
when locked or un-synced tracks would leave material behind.

### Split

Split divides items intersecting the playhead on targeted, unlocked tracks.
Source timing, effects, and link membership are preserved on both resulting
items. The two new items receive new IDs; the command result maps the old ID to
the replacements for selection and automation clients.

### Move

Move changes selected item positions while preserving internal timing and link
offsets. A move that would overlap an item on the same track is rejected in
version 0.1 rather than silently overwriting it. Insert-move and overwrite-move
are explicit later commands.

## Trim semantics

- **regular trim** changes one clip edge and leaves other item positions fixed;
- **ripple trim** changes one edge and shifts downstream items on sync-locked
  tracks by the inverse duration delta;
- **rolling trim** moves a shared cut between adjacent clips while keeping their
  combined sequence duration fixed;
- **slip** changes a clip's source in/out by the same amount while keeping its
  timeline range fixed;
- **slide** moves a clip in sequence time while rolling the outward-facing edges
  of its two neighbors, keeping the total covered range fixed.

Trim previews show the relevant outgoing and incoming frames. Commands clamp to
available media handles unless the item type explicitly supports generated
frames.

The current desktop implementation exposes slip on `Alt+Left/Right` and rolling
the selected clip's right-hand cut on `Ctrl+Alt+Left/Right`. Both operate on
linked A/V items as one undo transaction. Inspector fade-in/fade-out values are
frame counts, are drawn as ramps on timeline clips, and apply the same linear
envelope to preview video, preview audio where supported, and final export.
Fade handles can be dragged directly on a clip. Dissolve and audio-crossfade
durations can likewise be resized from the transition handle at the incoming
clip edge.

The Inspector can add, update, and remove motion keyframes at the playhead for
opacity, position, scale, rotation, and anchor. Motion keyframes support linear,
hold, and eased interpolation, are shown as green diamonds in the timeline, and
produce identical values in continuous playback, timeline preview, saved
projects, and final export. Direct Program-monitor manipulation updates the
current motion keyframe once animation exists.

Audio gain can also be automated at arbitrary clip-local frames. Linear, hold,
and eased gain keyframes are displayed as yellow diamonds connected by the gain
curve over the waveform. Inspector edits and gain-line drags create or update a
keyframe at the playhead once automation exists; preview mixing, continuous
playback, project persistence, and final export evaluate the same curve.

## Selection, snapping, and modifiers

Click selects one item; additive selection uses the platform-standard primary
modifier. Linked selection is enabled by default. Box selection operates only
on visible unlocked tracks.

Clicking one member of a link group selects the complete group. Holding `Alt`
while clicking temporarily selects only that item without destroying its link.

Snapping considers the playhead, markers, sequence in/out, item edges, and
transition edges. It uses a screen-pixel threshold converted into timeline time,
so the interaction remains stable at every zoom level. A modifier temporarily
disables snapping during a drag.

Platform shortcut labels differ (`Ctrl` on Windows, `Command` on macOS), but
commands use logical shortcut roles internally. User mappings are stored by
command ID rather than raw key event.

## Playback behavior

- Space toggles play/pause in the active monitor.
- J, K, and L control reverse, stop, and forward shuttle.
- Left/right step one sequence frame; audio-unit mode may step samples.
- Audio is the master clock during playback.
- If decoding falls behind, preview may lower resolution or drop video frames;
  it must not stretch sequence time or desynchronize audio silently.
- Scrubbing may use reduced-quality frames but the displayed timecode must always
  describe the requested sequence time.

`Shift+I` and `Shift+O` mark an exclusive sequence In/Out range; `Shift+X`
clears it. Playback restarts and stops within the marked bounds, `;` lifts the
range while leaving a gap, and `'` extracts it while closing sync-locked tracks.
Review export renders only the marked range when one exists and retimes its SRT
sidecar to start at zero. Lift/extract update captions atomically; extract also
removes enclosed markers and shifts later markers.

## Undo, autosave, and failure behavior

One user gesture produces one named undo transaction, even when it changes many
items. Continuous parameter drags coalesce until pointer release or focus loss.

Commands are journaled before success is reported. Background analysis and cache
generation are not part of edit history because their results are rebuildable.
If a media worker exits, transport stops, the project stays open, and the user
can restart the worker without reopening the project.

## Version 0.1 tool surface

Version 0.1 exposes selection, razor/split, regular trim, ripple trim, rolling
trim, slip, hand/pan, and zoom. Slide editing may be command-only until its trim
viewer interaction is polished.

The first property set includes position, scale, rotation, anchor, opacity,
audio gain, mute, playback rate, crop, masks, fades, and a basic dissolve.
Motion, speed, and built-in effect parameters can be keyframed. Advanced color,
nested sequences, multicam, and plugin effects remain later milestones.

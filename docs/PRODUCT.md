# Product definition

## Vision

Build the open editing surface that creators can trust with real projects and
that AI agents can operate without corrupting creative intent.

“Adobe-class” is a direction, not the first release. Videx should initially win
on one workflow: turning long recordings into polished, captioned videos quickly.

## Primary user

The initial user is a solo or small-team creator editing interviews, podcasts,
tutorials, and talking-head content on a desktop. They value speed, keyboard
control, transparent AI edits, and the ability to move a timeline to another
editor.

## Version 0.1 promise

Given common local video and audio files, a user can:

1. create and reopen a project without relinking surprises;
2. generate proxies, thumbnails, waveforms, and a transcript;
3. perform frame-accurate insert, overwrite, cut, ripple delete, slip, trim,
   track mute/lock, and simple gain/opacity edits;
4. play the timeline with synchronized audio;
5. ask AI to remove silences or text ranges, preview the proposed changes, and
   undo them as one transaction; and
6. export a review MP4 and an OpenTimelineIO interchange file.

## Explicit non-goals for 0.1

- motion graphics comparable to After Effects;
- collaborative cloud editing;
- third-party binary effect plugins;
- multicam, HDR finishing, color-managed delivery, or RAW camera pipelines;
- mobile and browser versions;
- generative video built into the core editor.

These may follow, but putting them into the first architecture would slow down
the one thing that must feel excellent: timeline editing.

## Design principles

### Local-first and non-destructive

Source media is immutable. A project contains edit decisions, metadata, and
references. Generated files are reproducible caches.

### Deterministic before intelligent

The same project state, command, and engine version must produce the same new
state. AI converts intent into commands; it is not part of playback or project
serialization.

### Responsive under imperfect conditions

The UI must remain interactive while media is analyzed, proxies are generated,
or export is running. Worker crashes must not take the project document down.

### Open escape hatches

The native project schema is documented and versioned. OpenTimelineIO is used at
the interchange boundary. Neither a cloud account nor a proprietary model is
required to open and edit a project.

## Success measures for the vertical slice

- first frame visible within 300 ms for warm proxy media;
- transport input-to-visible-frame latency below 100 ms in the reference setup;
- no audible discontinuity across ordinary cuts;
- 60 fps timeline interaction with 10,000 visible edit items;
- every document mutation undoable and replayable;
- crash recovery loses no more than the current command;
- a 30-minute 1080p dialogue project can be completed end-to-end.

Performance numbers are budgets to validate on a published reference machine,
not promises made before measurement.

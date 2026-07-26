# Videx implementation plan

## Objective

Deliver a Windows/macOS alpha that can import ordinary dialogue footage, create
proxies and waveforms, perform deterministic multitrack edits, play synchronized
audio/video, recover from failure, and export a review MP4. The same project must
open identically on both operating systems.

This is a twelve-week engineering plan for the first vertical slice. Dates begin
when both a Windows and a macOS build machine are available.

## Supported development targets

- Windows 11 x64 with MSVC;
- macOS 13 or newer with Apple Clang, initially Apple Silicon;
- macOS universal packaging (`arm64` and `x86_64`) before the alpha release;
- C++20 language baseline with CI-approved C++23 additions;
- Qt 6, CMake presets, Ninja, and pinned FFmpeg source/build configuration.

The build must not depend on one developer's globally installed package state.
Tool and dependency versions are recorded in the repository.

## Planned repository layout

```text
Videx/
├── CMakeLists.txt
├── CMakePresets.json
├── cmake/
├── src/
│   ├── app/                 Qt application and composition root
│   ├── core/                project, timeline, commands, undo, persistence
│   ├── media/               FFmpeg wrappers and media scheduling
│   ├── render/              render graph and RenderBackend
│   ├── ui/                  Qt widgets, timeline, monitors, inspector
│   └── platform/
│       ├── windows/         WASAPI and Direct3D integration
│       └── macos/           CoreAudio, VideoToolbox, and Metal integration
├── workers/
│   └── media-worker/        isolated probe/decode/render/export process
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── golden/
│   └── performance/
├── schemas/                 project and IPC schema versions
├── third_party/             manifests and build metadata, not vendored binaries
└── docs/
```

`core` is a Qt-free and FFmpeg-free library. UI and worker executables depend on
it, never the reverse.

## Week 1: reproducible skeleton

### Work

- Add root CMake project, presets, targets, install rules, and test discovery.
- Create a minimal Qt main window on Windows and macOS.
- Add formatting, warnings-as-errors, static analysis, and unit-test execution.
- Establish Windows and macOS CI with debug and release compilation.
- Pin Qt/FFmpeg acquisition and document local setup.
- Decide project license and the LGPL/GPL configuration used for distributed Qt
  and FFmpeg binaries.

### Exit gate

A clean machine can configure, build, test, and launch the same commit on both
systems using documented commands. No editor code begins until this is true.

## Week 2: architecture risk spikes

### Work

- Render a color test frame through `QRhiWidget` on Direct3D and Metal.
- Decode representative H.264, HEVC, and ProRes files in a media worker.
- Test hardware-frame import and a CPU-copy fallback on both systems.
- Implement experimental WASAPI and CoreAudio outputs with a monotonic clock.
- Benchmark a timeline widget containing 10,000 items.
- Kill the media worker during playback and verify the Qt process survives.

### Exit gate

Document measured seek latency, frame-transfer cost, timeline frame rate, audio
buffer behavior, and recovery behavior. Choose the initial frame-transfer path
from evidence. Replace QRhi before product work if it fails the spike.

## Weeks 3–4: editing core

### Work

- Implement IDs, rational time, assets, sequences, tracks, clips, links, and
  markers as platform-neutral value types.
- Implement insert, overwrite, lift, extract, split, move, and trim commands.
- Enforce timeline invariants and revision checks.
- Add transactions, inverse commands, undo/redo, journal, atomic snapshots, and
  crash recovery.
- Add generated/property tests for edit sequences and serialization.

### Exit gate

One million randomized command sequences complete without invariant violations,
sanitizer findings, or reopen-state differences. The same golden projects
serialize equivalently on Windows and macOS.

## Weeks 5–6: media ingestion and caches

### Work

- Probe assets and record streams, time bases, color metadata, and audio layouts.
- Implement stable source identity and missing-media relinking.
- Generate thumbnails, waveforms, and proxies as cancellable jobs.
- Add cache keys, quotas, invalidation, and rebuildable indexes.
- Surface progress and failures without blocking editing.

### Exit gate

A supplied mixed-codec media set imports on both systems, survives relocation,
and regenerates identical cache metadata. Cancelling or killing a job does not
damage project state.

## Weeks 7–8: playback and export

### Work

- Compile sequence edits into immutable render plans.
- Add decode-ahead, frame cache, seeking, play/pause, and J/K/L transport.
- Make audio the master playback clock and add audio meters.
- Render cuts, opacity, gain, playback rate, and a basic dissolve.
- Export deterministic review MP4 files with progress and cancellation.
- Add A/V impulse, timecode, golden-frame, and golden-audio tests.

### Exit gate

A 30-minute 1080p dialogue sequence plays from proxies without perceptible A/V
drift on both reference systems and exports with matching duration and edit
boundaries.

## Weeks 9–10: editing interface

### Work

- Build project/bin, source monitor, program monitor, timeline, inspector,
  history, jobs, and audio-meter panels.
- Implement track targeting, source patching, sync lock, linked selection,
  snapping, zoom, and keyboard mapping.
- Add regular, ripple, rolling, and slip trim interactions and trim monitors.
- Add autosave status, recovery UI, diagnostics, and missing-media workflows.
- Validate Windows and macOS shortcuts, HiDPI behavior, menus, and file dialogs.

### Exit gate

Scripted editing tasks can be completed on both systems with mouse or keyboard,
and identical command logs result from equivalent actions.

## Week 11: AI command workflow

### Work

- Add transcript storage linked to source time ranges.
- Define bounded AI tools for search, silence removal, transcript-range removal,
  markers, and captions.
- Present proposed changes as a readable timeline diff.
- Commit accepted proposals as one ordinary undo transaction.
- Provide one local transcription adapter; keep remote providers optional.

### Exit gate

AI has no project-file or filesystem mutation path. Recorded command proposals
can be replayed without a model and produce identical project revisions.

## Week 12: alpha hardening

### Work

- Run corrupt-media, worker-crash, low-disk, GPU-reset, and interrupted-save tests.
- Profile CPU, GPU, memory, cache growth, seek latency, and startup time.
- Produce signed Windows packaging and notarized macOS universal packaging.
- Generate third-party notices and publish exact Qt/FFmpeg build configurations.
- Write the alpha test guide and known limitations.

### Exit gate

All release checks pass on both systems, no blocker-level data-loss issue remains,
and a fresh user can complete the reference edit from import through export.

## Continuous rules

- Every pull request builds and tests on Windows and macOS.
- Core behavior changes require unit tests and an editor-spec update.
- Media or render changes require a regression fixture or benchmark.
- No platform-specific behavior enters `src/core`.
- No feature is complete until failure, cancellation, and undo behavior are known.
- Performance regressions are tracked against published reference machines.

## Deferred until after alpha

- HDR finishing and a complete color-management pipeline;
- multicam and collaborative editing;
- third-party binary effect plugins;
- motion graphics comparable to After Effects;
- cloud project storage;
- mobile, Linux, or browser versions;
- generative video inside the core editor.

## Immediate next tasks

Implementation snapshot:

1. The CMake/Qt skeleton, Windows and macOS presets, and dual-platform CI
   workflow are present. Windows builds and tests locally; macOS CI still needs
   to be exercised on the remote repository.
2. The Qt/QRhi monitor surface and isolated FFmpeg probe/decode worker are
   implemented. RGBA frames and normalized 48 kHz stereo audio use validated
   binary envelopes; audio drives the preview clock when available. Hardware
   frame-transfer, device-loss, and worker-crash measurements remain open.
3. The deterministic core now has stable IDs, tracks/clips, insert, overwrite,
   move, split, lift, extract/ripple delete, regular trim, persistent track
   lock/mute/visibility, revision checks, atomic transactions, and undo/redo with
   unit coverage.
4. Schema-v1 `.videx` project persistence uses atomic writes and lossless decimal
   strings for 64-bit identifiers and frame values. Round-trip coverage is in
   the test suite.
5. The desktop shell can import media, continuously play synchronized audio/video,
   seek from a transport slider or timeline ruler, shuttle with J/K/L, step frames,
   and reopen projects. Linked A/V move/split/lift/trim, pixel-threshold snapping,
   track state, undo/redo, recovery autosaves, and missing-media relinking are
   implemented. The isolated worker now exports timeline cuts, opacity,
   playback-rate, gain, audio/video fades, and mixed 48 kHz stereo audio to
   H.264/AAC MP4 with progress, cancellation, and atomic publish. Deterministic
   thumbnail/waveform caches and 540p proxies run as background jobs. Markers,
   Program-monitor captions, `.srt` sidecars, slip/rolling/slide trims, an
   Inspector, audio meters, and runtime diagnostics are implemented. Motion,
   speed, gain, and built-in effect automation is persisted and evaluated by
   both preview and export. The asset cache is pruned to a quota (default
   4 GiB; `VIDEX_CACHE_QUOTA_MB` overrides it) while caches referenced by the
   open project are always retained, and File > Export OpenTimelineIO writes an
   `.otio` interchange file (clips, gaps, tracks, markers, linear speed) with
   an explicit compatibility report for unrepresented effects. Insert shifts
   markers and captions right so insert followed by extract round-trips
   sequence annotations. macOS CI still needs a successful remote run;
   hardware decode, a general effect graph, transcript editing, performance
   qualification, and packaging remain milestones.
6. Timeline multi-track status: tracks are grouped by kind (video above audio;
   the display stacks the front-most video track on top with V1 just above
   A1), new video tracks insert into the video group, and track moves are
   restricted to their own kind. Tracks carry kind-relative names (V1/A1),
   optional custom name/color, an audio Solo flag that gates playback and
   export mixing, and a persisted height preset (minimal/standard/expanded via
   the track context menu). The timeline scrolls vertically (wheel; Shift+
   wheel pans time, Ctrl+wheel zooms), rows are culled while painting, and
   the ruler plus subtitle lane stay fixed. The render plan carries an
   explicit per-kind track order: video composites V1 (back) to the highest
   track (front) in both preview and export, and transitions only pair clips
   that are adjacent on the same track (regression-tested). Dragging a clip
   shows a live vertical preview (green/red validity, ghost at the origin,
   forbidden cursor over other-kind rows); dropping onto another same-kind
   track applies the same kind-relative shift to linked clips, and dragging a
   video clip into the band above the top row (or dropping an asset there, or
   below the last audio row) creates a new track automatically. Titles are
   ordinary video clips backed by an app-rendered 1920x1080 transparent raw
   still (.vxtitle; the FFmpeg build ships no image decoders, so the media
   layer has a dedicated reader that scales the still to any output size in
   both directions). T or the Text panel creates one on a free video track,
   so motion, opacity, crop, effects, keyframes, trims, splits, and
   copy/paste work unchanged; preview/export parity and up-scaling are
   regression-tested, the title style persists in asset metadata inside the
   project (round-trip tested, Japanese included), and missing rasters are
   regenerated from metadata on load, before export, and while building
   preview manifests. Track creation via drop zones or drag commits as ONE
   atomic transaction (a null placement target binds to the track added in
   the same transaction), so undo removes track+clip together and a failed
   placement leaves no empty track. Loading normalizes legacy interleaved
   track orders (video group first, relative order kept). Dragging supports
   Alt-drop duplication (properties and links preserved via paste
   placements), auto-scrolls at the viewport edges, shows target-row or
   new-track ghosts for external asset drags, and releasing over an invalid
   (locked/colliding) track cancels the drop. Track heights can also be
   dragged at the row boundary in the label column (snaps to the three
   presets). Captions remain as the subtitle lane with SRT export; the lane
   stacks overlapping captions into up to four sub-rows and offers
   Convert-to-Title. Not yet implemented: drag-reorder of tracks, multi-track
   source patching (targets stay one video + one audio track), per-track
   name/color editing UI, nested sequences, adjustment layers, and
   virtualized painting for thousands of clips. Not covered by automated
   tests (manual checks): mute/solo/enable audibility in rendered output,
   real drag-and-drop event delivery visuals, and GUI undo/redo stress.
7. P3 (editing UX) status: the Program Monitor supports direct Position/Scale/
   Rotation/Anchor/Crop manipulation with hover cursors, transient drag preview,
   one-undo-per-gesture, Escape cancel, zoom/pan (Fit/percent, Ctrl+wheel,
   Space/middle-drag), an overlay settings menu, and click-to-select of the
   topmost video clip under the playhead. Inspector numeric labels scrub with
   Shift/Ctrl/Alt step modifiers. Effect Controls include an applied-effect
   list, per-property keyframe lanes with interpolation editing and reorder,
   plus an Effects Browser with search and drag-apply. Text clips have a
   panel-based editor (font, size, colors, bold/italic, position) with
   debounced typing (one undo per burst); captions occupy a dedicated
   timeline lane below the ruler where they can be dragged, selected,
   double-clicked to edit, and deleted. On the Program Monitor the caption
   overlay can be clicked to focus the Text panel, dragged to reposition
   (one undo per drag, Escape cancels), and double-clicked to edit text.
   Clicking the canvas outside the picture clears the selection; committed
   transform/crop edits hold their preview until the re-rendered frame
   arrives (no jump-back), percentage zoom is anchored to the 1280x720
   reference so playback-resolution changes do not resize the picture, and
   double-click no longer resets zoom. Loading a project links legacy
   unlinked A/V pairs, and move-drag snapping never pins a clip to its
   origin. On-monitor text scale/rotation handles are not yet implemented.
   Playback has resolution
   divisors, loop, play in-out, fullscreen preview, Render In-to-Out preview
   cache with a timeline cache bar, and a diagnostics overlay. Export offers
   preset/size/bitrate selection with validation and estimated file size.
   Trim-style drags (regular/ripple) preview the frame at the moving edit
   point with a timecode overlay in the Program Monitor, and rolling edits
   show a two-up (outgoing | incoming around the cut, labeled OUT/IN). UI
   interaction tests cover monitor drags and timeline pointer edits,
   including trim preview streaming.

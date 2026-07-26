# First 90 days

The objective is evidence that the architecture can support a professional-feel
editor, not a long feature checklist.

## Weeks 1–2: foundation and risk spikes

- Establish CMake presets, C++ formatting/static analysis, unit tests, and CI on
  Windows and macOS.
- Decide the reference machines and publish repeatable benchmarks.
- Prototype a Qt timeline widget with 10,000 items, snapping, zooming, and
  keyboard-driven selection.
- Prototype a QRhi monitor surface using Direct3D on Windows and Metal on macOS;
  verify resize, HiDPI, fullscreen, and GPU-device loss.
- Decode H.264 and ProRes samples through an isolated FFmpeg worker and transfer
  frames without copying full pixels through JSON/IPC.
- Build a 48 kHz audio callback and prove seek/play/pause synchronization.

Exit criterion: the same source tree builds and runs on both systems, and
measured results choose the GPU-frame-transfer and audio-clock mechanisms. QRhi
is replaceable behind `RenderBackend` if its compatibility or performance is
insufficient.

## Weeks 3–5: deterministic editing core

- Define versioned project schema v0 and rational-time primitives.
- Implement assets, sequences, tracks, clips, gaps, and stable IDs.
- Implement split, insert, overwrite, move, trim, and ripple delete commands.
- Add transactions, revision checks, undo/redo, journal, snapshot, and recovery.
- Add property tests and a headless command-line harness.

Exit criterion: randomized command sequences preserve invariants and reopening a
project reproduces the exact state.

## Weeks 6–8: media vertical slice

- Import/probe media and create deterministic cache identities.
- Generate proxies, thumbnails, and waveforms in cancellable background jobs.
- Compile a simple edit graph and play cuts with audio as master clock.
- Add opacity, gain, a basic dissolve, and deterministic software export.
- Render an H.264 review file with progress, cancel, and resumable job metadata.

Exit criterion: complete a supplied 30-minute dialogue project without leaving
Videx and meet the published responsiveness budgets on proxy media.

## Weeks 9–10: editor UX

- Create source/program viewers, bin, timeline, inspector, meters, and job panel.
- Add keyboard mapping, snapping, track targeting, linked selection, and ripple
  modes.
- Add autosave status, missing-media relink, error recovery, and diagnostics.

Exit criterion: five scripted editing tasks can be completed reliably using
mouse or keyboard and survive an intentional worker crash.

## Weeks 11–12: AI-native workflow

- Create transcript segments linked to source time ranges.
- Implement `remove_silence`, `remove_transcript_ranges`, and caption-generation
  command proposals.
- Show a human-readable diff, timeline preview, accept/reject, and one-step undo.
- Add one local transcription adapter and one optional remote adapter.

Exit criterion: AI never mutates state outside the command API, and identical
accepted commands yield identical project state without an AI provider present.

## Decisions deliberately deferred

- final desktop shell until native-preview spikes are measured;
- application license until FFmpeg distribution profile and plugin goals are
  agreed;
- collaboration data structures until the single-writer command model is proven;
- effect plugin ABI until the render graph and color pipeline mature;
- HDR/color-management architecture until after the editing vertical slice.

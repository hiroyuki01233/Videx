# Editor UI/UX feature inventory

This inventory defines the baseline editing surface independently of any one
commercial editor. P0 through P2 are the usable-alpha scope; later finishing
features are explicitly separated from the long-term Adobe-class goal.

## P0 — essential editing loop

- Project creation/open/save, atomic autosave, recovery, relink, import, bins,
  thumbnails, waveforms, and proxies.
- Source and Program monitors with seek, source In/Out, sequence In/Out,
  timecode, Play/Pause, J/K/L shuttle, and frame stepping.
- Video/audio track creation, reorder/delete, target, source patch, lock,
  visibility, mute, and sync lock.
- Selection, linked A/V selection, additive and box selection, Alt link
  override, snapping, zoom, scroll/pan, and playhead dragging.
- Insert, overwrite, move across tracks, split/razor, lift, extract/ripple
  delete, cut/copy/paste, duplicate, undo/redo, and named history.
- H.264/AAC review export with progress/cancel/atomic publish and SRT sidecar.

## P1 — precise timeline operation

- Regular, ripple, rolling, slip, and slide edits.
- Direct fade and transition handles, video dissolve, audio crossfade, and
  sequence-range lift/extract.
- Clip position, scale, rotation, anchor, opacity, crop, mask, speed, audio
  gain, and linked-property editing.
- Markers, styled captions/titles, search/filter, media deletion/relink, proxy
  controls, diagnostics, and audio meters.
- Context menu for split, clipboard actions, duplicate, fades, transitions,
  link/unlink, reset, effect removal, lift, and ripple delete.

## P2 — keyframing and preview/export parity

- Motion keyframes for position, scale, rotation, anchor, and opacity.
- Speed ramps and built-in effect keyframes.
- Audio gain keyframes for arbitrary fades, dips, and dialogue ducking.
- Linear, hold, and eased interpolation with timeline diamond/curve feedback.
- Multitrack compositing and audio mixing using the same evaluated properties in
  stopped preview, continuous playback, saved projects, and final export.
- Context-menu creation/removal of motion and gain keyframes at the playhead.
- Brightness, contrast, saturation, blur, vignette, rectangle/ellipse masks,
  styled captions, and review overlays.

## Post-P2 professional finishing backlog

- Color-managed Rec.709/Rec.2020/HDR pipeline, scopes, LUTs, curves, secondary
  correction, tracking, stabilization, chroma key, and advanced masking.
- Nested sequences, compound clips, adjustment layers, multicam, time-remap
  graphs, optical flow, freeze frames, and replace edit.
- Audio mixer buses, pan automation, EQ, compressor, limiter, loudness targets,
  noise repair, sidechain routing, and plugin hosting.
- Title templates, motion graphics, third-party effects, OFX/VST support, XML/
  AAF/EDL interchange, background render, render cache, and export presets.
- Configurable shortcuts/workspaces, accessibility audit, HiDPI/multi-monitor
  polish, crash reporting, signed Windows packaging, notarized macOS universal
  packaging, and measured cross-platform performance gates.
- Transcript editing, local transcription, silence removal proposals, semantic
  search, edit diff/accept/reject, and deterministic AI command replay.

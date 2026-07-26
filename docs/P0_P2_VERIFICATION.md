# P0–P2 verification matrix

This matrix ties the usable-alpha scope to implementation and executable
evidence. A feature is not considered covered merely because it is listed in a
plan.

| Scope | Requirement | Implementation evidence | Executable evidence |
|---|---|---|---|
| P0 | Project save/open, stable IDs, autosave/recovery | `MainWindow::saveProject`, `openProject`, `autosaveProject`; `project_file.cpp` | `videx.project_file.round_trip` |
| P0 | Import, source range, insert/overwrite, bins, caches/proxies | `importMedia`, `openAssetInSource`, `insertSourceSelection`, `startAssetCacheJobs`, `generateProxy` | media probe/protocol tests; Release GUI smoke |
| P0 | Play/Pause, J/K/L, seek, frame step, timeline playhead | `createActions`, `startPlayback`, `requestPlaybackAudio`, `requestTimelineFrame` | `videx.timeline.pointer_interactions`; media-worker protocol test |
| P0 | Split, move, lift, extract, clipboard, linked A/V, undo/redo | typed commands in `timeline.cpp` and dispatch in `edit_session.cpp` | `videx.core.application_info` contains command/invariant/undo tests |
| P0 | Track lock/mute/visibility/sync lock/targeting | track commands plus timeline track controls | core track tests; Release GUI smoke |
| P0 | H.264/AAC review export, range export, captions | `exportReview`, `writeCaptionSidecar`, `exportTimeline` | `videx.media.export_h264_aac` |
| P1 | Regular/ripple/rolling/slip/slide edits | corresponding `Sequence` commands and timeline tools | core trim/roll/slip/ripple/slide tests |
| P1 | Direct fades and transition handles | timeline drag modes and `setSelectedFades`/`setSelectedTransitions` | pointer fade test; core fade/transition tests |
| P1 | Transform, crop, mask, speed, gain, effects, captions | Inspector commands and persisted clip properties | core validation/undo tests; project round trip; export pixel test |
| P1 | Box/additive/linked selection and snapping | `TimelineWidget` selection and `snapFrame` | pointer box-selection test; linked core tests |
| P1 | Context menu editing | `TimelineWidget::ContextAction` and MainWindow transaction handler | compiled with warnings-as-errors; Release GUI smoke |
| P2 | Motion automation | motion keyframe commands, Inspector, Program-monitor manipulation, renderer | core retime/undo test; project round trip; export pixel test |
| P2 | Speed and effect automation | speed/effect keyframe commands and render evaluation | core retime/undo tests; project round trip; export test |
| P2 | Arbitrary-position audio gain automation | gain commands, pointer-frame drag, yellow curve/diamonds, audio mixer | pointer targeting test; core split/trim/undo test; project round trip; protocol RMS-envelope test |
| P2 | Preview/export property parity | shared manifest fields and matching interpolation evaluators | frame/audio worker protocol and H.264/AAC export tests |

## Platform gate

- Windows x64 MSVC Release: configure/build, all CTest cases, deployed runtime,
  and GUI startup are verified locally.
- macOS Apple Clang: source boundaries, preset, bundle target, worker lookup,
  codec selection, and GitHub Actions job are present. A macOS runner result is
  still required as platform execution evidence; Windows results cannot prove
  Metal/CoreAudio/VideoToolbox or bundle deployment behavior.

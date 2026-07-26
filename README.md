# Videx

Videx is an open-source, AI-native non-linear video editor. The long-term goal is
professional editing quality; the first milestone is deliberately smaller: a
fast, reliable desktop editor for cutting dialogue-heavy video.

The project is in early implementation. The Windows development build currently
launches a Qt editor shell, probes and decodes media through an isolated FFmpeg
worker, displays real source/program frames, plays synchronized audio/video,
generates thumbnails, waveforms, and 540p proxies, and supports linked
move/split/lift plus regular/ripple/rolling/slip edits with undo/redo. Inspector
opacity, position, scale, rotation, anchor, gain, playback-rate, crop, masks,
audio/video fades, built-in effects, and motion/speed/gain/effect keyframes survive
project reload and are applied to preview and review exports. Markers and
captions are stored in the project;
captions preview in the Program monitor and export as an adjacent `.srt` file.
Projects use an atomic `.videx` format with autosave recovery and media relink.
Windows and macOS share the same C++ source tree; platform GPU/audio integrations
remain behind explicit boundaries.

## Product thesis

- Editing remains deterministic and non-destructive.
- AI proposes the same typed commands a human action uses; it never rewrites a
  project file or media directly.
- Playback responsiveness and audio correctness take priority over the number of
  effects.
- Projects remain usable offline and are stored in an open, versioned format.
- Interchange is a first-class feature, not an export afterthought.

## Initial target

The first vertical slice imports local media, creates proxies and waveforms,
supports multitrack cut/trim/move operations with undo/redo, previews the result,
and exports an H.264 review file. A transcript can then drive the exact same edit
commands.

## Design documents

- [Product definition](docs/PRODUCT.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Editor behavior specification](docs/EDITOR_SPEC.md)
- [Editor UI/UX feature inventory](docs/UX_FEATURE_INVENTORY.md)
- [P0–P2 verification matrix](docs/P0_P2_VERIFICATION.md)
- [First 90 days](docs/ROADMAP.md)
- [Implementation plan](docs/IMPLEMENTATION_PLAN.md)
- [ADR 0001: technical foundation](docs/adr/0001-technical-foundation.md)
- [ADR 0002: AI editing boundary](docs/adr/0002-ai-command-boundary.md)
- [ADR 0003: timeline semantics](docs/adr/0003-timeline-semantics.md)
- [ADR 0004: C++ and Qt foundation](docs/adr/0004-cpp-qt-foundation.md)
- [Development setup](docs/DEVELOPMENT.md)

The initial implementation uses modern C++, Qt 6, CMake, and FFmpeg in one
Windows/macOS codebase. GPU and operating-system integrations remain behind
Videx-owned interfaces so they can be replaced when measurements require it.
The project license remains to be selected before the first binary release.

## Build and run

See [Development setup](docs/DEVELOPMENT.md) for dependency setup and presets.
On a configured Windows machine, the normal verification loop is:

```powershell
cmake --preset windows-media
cmake --build --preset windows-media --parallel
ctest --preset windows-media --output-on-failure
./build/windows-media/bin/Debug/Videx.exe
```

Use **File > Export Review MP4** (`Ctrl+M`) to render the current sequence. The
worker writes to a temporary `.part.mp4` and publishes the requested file only
after the MP4 trailer has been finalized successfully.

Useful editing shortcuts include Space and J/K/L for transport, Left/Right for
frame stepping, `M` for markers, `Ctrl+Shift+T` for captions, `Alt+Left/Right`
for slip edits, and `Ctrl+Alt+Left/Right` for rolling the selected clip's right
cut. `Shift+I`/`Shift+O` mark a sequence range, `;` lifts it, and `'` extracts
it; review export uses the marked range when present. Runtime paths and build
information are available under **Help >
Diagnostics**.

# Videx

Videx is an open-source, AI-native non-linear video editor. The long-term goal is
professional editing quality; the first milestone is deliberately smaller: a
fast, reliable desktop editor for cutting dialogue-heavy video.

The project is currently in its design phase.

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
- [First 90 days](docs/ROADMAP.md)
- [ADR 0001: technical foundation](docs/adr/0001-technical-foundation.md)
- [ADR 0002: AI editing boundary](docs/adr/0002-ai-command-boundary.md)

No implementation stack or project license is final until the architecture
spikes in the roadmap have been measured.

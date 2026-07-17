# Architecture

## Shape of the system

Videx starts as a modular desktop monolith with isolated media workers. It is not
a collection of network services.

```text
+---------------- Desktop application ----------------+
|                                                     |
|  TypeScript UI                                      |
|  panels / timeline canvas / keyboard / accessibility|
|                 | typed messages                    |
|  Rust application core                             |
|  project / commands / undo / jobs / persistence     |
|       |                 |                |           |
|  native preview   media worker(s)      AI adapter   |
|  wgpu surface     decode/render/export  local/cloud  |
|       +--------- shared frames/handles --+           |
+-----------------------------------------------------+
          |                |
     source media     cache/proxies/indexes
```

The media boundary is process-isolated because codecs consume untrusted input,
export can be long-running, and a decoder fault should not destroy unsaved edit
state.

## Components

### UI

The UI owns transient presentation state: selection, hover, panel layout, zoom,
and drag previews. It renders the timeline as a virtualized canvas rather than a
large DOM tree. It does not own the authoritative project.

While dragging, the UI can render an optimistic preview. On commit it sends one
domain command and reconciles with the resulting project revision.

### Application core

The core is the single writer for project state. Its API is a small set of typed
queries and commands. Responsibilities include:

- validating commands against the current project revision;
- applying atomic transactions and generating inverse operations;
- appending to the recovery journal and scheduling snapshots;
- resolving asset identities and cache keys;
- coordinating background jobs; and
- exposing stable events to the UI and automation clients.

### Media engine

The media engine uses FFmpeg libraries for demux, decode, encode, resampling, and
format conversion. A separate render graph describes compositing and effects.
GPU effects and preview composition are implemented behind a wgpu abstraction;
CPU fallbacks remain possible for correctness and testability.

The first engine has four schedulers:

- realtime preview, which may reduce resolution or drop video frames but never
  advances audio incorrectly;
- audio, which supplies a monotonic clock and prebuffered samples;
- analysis, for metadata, thumbnails, waveforms, scene data, and transcripts;
- export, which is deterministic and never drops frames.

### AI adapter

AI receives a bounded project view and returns a proposed command transaction.
It has no filesystem path, raw SQL, project-file write, or media-delete tool.
More detail is in ADR 0002.

## Project data model

The authoritative model is an edit decision graph, not a list of UI rectangles.
All persisted entities have stable opaque IDs.

```text
Project
├── assets[]        source identity, streams, metadata, proxy references
├── sequences[]
│   ├── settings    rational frame rate, dimensions, audio layout
│   ├── tracks[]    kind, order, lock/mute/visibility
│   │   └── items[] clip | gap | transition | nested-sequence
│   └── markers[]
├── transcripts[]  time-ranged words linked to asset streams
└── metadata        schema version, app version, creation data
```

Each clip stores a timeline range, a source range, playback rate, asset stream
reference, and ordered effect parameters. Time is represented by integer ticks
with an explicit rational rate; floating-point seconds are never persisted.

Track-item ordering and overlap rules are invariants enforced by the core. A UI
gesture such as ripple trim may expand into several low-level operations, but it
is committed as one transaction.

## Commands and history

A command envelope contains:

```text
command_id, project_id, base_revision, actor_id, timestamp,
command_type, payload, optional_group_id
```

Commands are validated, applied in memory, appended to a checksummed journal,
then acknowledged. Periodic atomic snapshots bound startup time. Undo creates a
new inverse transaction rather than moving a hidden mutable pointer. This makes
history inspectable and gives future collaboration a viable foundation without
requiring CRDT complexity in version 0.1.

## Files and caches

The working project is a directory bundle during early development:

```text
example.videx/
├── project.json
├── journal.log
├── cache-index.sqlite
└── cache/
```

Media is referenced, not copied by default. Asset identity combines normalized
location, size, modification time, and a sampled content hash; a full hash can be
computed when portability requires it. Cache keys include source identity,
operation parameters, engine version, and color/audio interpretation.

SQLite is used only for rebuildable indexes and job state. The canonical project
remains a readable, versioned document plus journal so database corruption does
not trap the edit.

## Playback path

1. The UI requests a sequence time and quality target.
2. The core compiles the affected edit graph into an immutable render plan.
3. Workers fetch proxy or source frames, decode ahead, and execute the graph.
4. Frames remain native/GPU-backed where supported and are presented on a native
   preview surface; full-resolution pixels are not serialized through UI IPC.
5. Audio drives the playback clock. The video scheduler selects the appropriate
   frame for that clock.

## Interchange

OpenTimelineIO is an import/export boundary, not the in-memory project model. It
represents editorial clips, tracks, transitions, markers, and metadata well, but
not every Videx effect or embedded media. Round trips therefore produce an
explicit compatibility report instead of silently discarding unsupported data.

## Testing strategy

- pure command tests assert invariants, inverses, and deterministic serialization;
- generated timelines fuzz trim, ripple, split, overlap, and undo sequences;
- golden-frame and golden-audio tests validate the render graph;
- A/V sync tests use synthetic timecode and impulses;
- corrupt/truncated media and worker termination test isolation and recovery;
- performance fixtures track seek latency, dropped frames, cache behavior, and
  timeline interaction across reference machines.

## Security and privacy

Projects and source media stay local by default. Cloud AI requires an explicit
provider and shows what context leaves the machine. Imported media is untrusted;
parsing and decoding occur outside the document process. Plugins, when added,
will be capability-scoped and out of process.

# ADR 0001: Technical foundation

- Status: Superseded by ADR 0004
- Date: 2026-07-18

## Context

A professional editor needs a dense, flexible UI, frame-accurate media behavior,
low-latency audio, GPU compositing, crash isolation, and cross-platform delivery.
No single framework supplies all of these well.

## Proposed decision

- Use Rust for the authoritative application core, job orchestration, IPC types,
  and new performance-sensitive infrastructure.
- Use TypeScript and React for panels and application UI, with a custom
  canvas/WebGL timeline renderer.
- Use Tauri 2 as the first shell candidate, not as an irreversible dependency.
- Present video through a native wgpu surface rather than an HTML video element.
- Use FFmpeg libraries in isolated workers for media I/O and codec operations.
- Use a Videx-owned render graph; do not make FFmpeg filter strings or MLT XML the
  canonical edit model.
- Use OpenTimelineIO adapters at interchange boundaries.

## Why not adopt an existing editor engine wholesale?

MLT is mature and extensible and can accelerate experiments, but making its
service graph the product model would couple UI semantics, persistence, and
future AI operations to an older abstraction. It remains valuable as a reference
and possible adapter. The highest-value Videx work is the deterministic command
model, responsive scheduler, and open automation boundary.

## Consequences

The split lets UI contributors work with common web tooling while media work
stays native. It also creates hard problems early: native preview embedding,
shared GPU resources, platform audio backends, and packaging FFmpeg correctly.
The roadmap therefore treats these as kill-or-confirm spikes before feature work.

## Licensing constraint

FFmpeg is primarily LGPL 2.1-or-later, while enabling optional GPL components
changes the resulting FFmpeg build to GPL. Codec patents are separate from
copyright licensing and vary by distribution jurisdiction. Videx must publish a
reproducible FFmpeg build profile and choose its own project license before the
first distributable release. This ADR does not make that legal choice.

## References

- [FFmpeg libraries and documentation](https://ffmpeg.org/documentation.html)
- [FFmpeg license and legal considerations](https://ffmpeg.org/legal.html)
- [OpenTimelineIO overview](https://opentimelineio.readthedocs.io/en/latest/)
- [Tauri architecture](https://v2.tauri.app/concept/architecture/)
- [wgpu project and platform backends](https://github.com/gfx-rs/wgpu)
- [MLT framework design](https://www.mltframework.org/docs/framework/)

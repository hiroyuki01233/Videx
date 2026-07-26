# ADR 0004: C++ and Qt foundation

- Status: Accepted
- Date: 2026-07-18
- Supersedes: ADR 0001

## Context

Videx targets professional desktop editing on Windows and macOS. Its critical
paths touch FFmpeg, GPU textures, low-latency audio, hardware codecs, native file
access, packaging, and future plugin APIs. A single implementation language and
native desktop toolkit reduce cross-language ownership and debugging boundaries.

ADR 0001 proposed Rust, TypeScript, Tauri, and wgpu. After reviewing the product
shape, the project prefers direct access to the mature C/C++ media and desktop
ecosystem and a fully native UI.

## Decision

- Use C++20 as the portability baseline. Selected C++23 features may be enabled
  only after they pass both MSVC and Apple Clang CI.
- Use Qt 6 Widgets for the desktop shell, panels, docking, input, accessibility,
  and platform integration.
- Use custom Qt widgets for the timeline and program/source monitors.
- Begin GPU presentation with `QRhiWidget`, isolated behind a Videx-owned
  `RenderBackend` interface because the underlying QRhi APIs have limited
  compatibility guarantees.
- Use FFmpeg libraries for media I/O and codec work in isolated worker processes.
- Use CMake presets and Ninja as the canonical build interface.
- Keep Windows and macOS platform adapters in the same repository and build both
  continuously.
- Do not use Rust, Tauri, React, or a WebView in the version 0.1 application.

## C++ safety profile

- No owning raw pointers and no application-level manual `new` or `delete`.
- RAII and value types by default; `std::unique_ptr` for dynamic ownership.
- Shared ownership requires a documented lifetime reason.
- Mutable state has one owner; threads communicate through queues and immutable
  messages rather than shared project objects.
- No exception, Qt type, STL container ABI, or C++ class ABI crosses a process or
  third-party plugin boundary.
- Compiler warnings are errors in project code.
- Address/undefined-behavior sanitizers, static analysis, and fuzz tests run in
  supported CI configurations.
- Untrusted media decoding remains outside the document process.

AI assistance does not relax this profile. Generated changes must pass the same
reviews, tests, sanitizers, and platform builds as human-written code.

## Consequences

C++ provides direct access to FFmpeg and native platform APIs and avoids a Rust
or JavaScript bridge on critical frame and audio paths. Qt supplies a shared
desktop UI while still allowing native Direct3D and Metal integrations.

The cost is greater exposure to memory, lifetime, and concurrency errors and a
more complex dependency toolchain. The safety profile, worker isolation, and CI
matrix are therefore architectural requirements rather than optional cleanup.

The Qt and FFmpeg distribution profiles must be reviewed alongside the Videx
license before shipping binaries.

## References

- [Current ISO C++ standard](https://isocpp.org/std/the-standard)
- [Qt supported platforms](https://doc.qt.io/qt-6/supported-platforms.html)
- [QRhiWidget](https://doc.qt.io/qt-6/qrhiwidget.html)
- [FFmpeg hardware contexts](https://ffmpeg.org/doxygen/trunk/hwcontext_8h.html)
- [Qt licensing](https://doc.qt.io/qt-6/licensing.html)

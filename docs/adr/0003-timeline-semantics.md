# ADR 0003: Timeline semantics

- Status: Accepted for the initial editor
- Date: 2026-07-18

## Context

An editor can look familiar while producing surprising results. Insert,
overwrite, ripple, linking, track locks, and trim modes interact across many
tracks. If these rules live only in UI event handlers, human actions, keyboard
commands, and AI edits will diverge.

## Decision

Timeline behavior is defined in the application core as typed domain commands.
The initial interaction model follows established professional source/program
editing conventions.

- Empty time is implicit; it is not stored as a gap object.
- Items use half-open rational-time ranges.
- Same-track overlap is invalid except for an explicit transition model.
- Audio and video streams are separate items connected by stable link groups.
- Track targeting, source patching, sync lock, and content lock are independent.
- Insert, overwrite, lift, extract, split, move, and each trim mode are distinct
  commands rather than flags on one generic mutation.
- Multi-item consequences commit as one revision-checked transaction.
- UI gestures and AI proposals call exactly the same commands.

The normative user-facing behavior is documented in `docs/EDITOR_SPEC.md`.

## Consequences

The model is more verbose than arbitrary item mutation, but it makes edits
predictable, testable, explainable, and undoable. Explicit commands also allow
automation clients to survive UI redesigns.

Version 0.1 rejects ambiguous collisions instead of guessing overwrite or ripple
intent. Additional edit modes can be added as new commands without weakening the
core invariants.

# ADR 0002: AI uses the command boundary

- Status: Accepted for the initial architecture
- Date: 2026-07-18

## Context

Giving a model direct access to the project document is easy to demo but unsafe:
schema mistakes, hallucinated IDs, non-undoable changes, and silent media damage
would make the editor untrustworthy.

## Decision

Humans, scripts, and AI all mutate a project through the same typed command API.
AI operates in two phases:

1. **Plan:** read a bounded, redacted project view and produce a command proposal
   with stable entity IDs and an explanation.
2. **Commit:** the core validates the base revision, permissions, invariants, and
   resource limits; the UI shows a diff; accepted commands commit atomically.

Initial AI-callable operations are semantic and narrow:

- search transcript and asset metadata;
- create a sequence from selected source ranges;
- remove silence or selected transcript ranges;
- move, split, trim, or delete identified items;
- add markers and captions; and
- change whitelisted scalar properties.

There is no generic `write_project`, `run_shell`, or `delete_media` operation.

## Guardrails

- Proposals include `base_revision`; stale proposals must be rebased or rejected.
- IDs must come from the supplied view; unknown IDs fail validation.
- A proposal has operation-count and affected-duration limits.
- Destructive-looking ripple changes include the full affected-range summary.
- Acceptance creates one named undo transaction.
- Provider prompts and outputs may be recorded only with user consent; committed
  typed commands remain in normal edit history.
- Cloud providers receive transcripts or low-resolution derived media only when
  the user explicitly enables that transfer.

## Consequences

Every AI feature is testable without an LLM by supplying a command proposal.
Users can inspect and undo edits, and local or cloud models are replaceable. The
tradeoff is that new creative capabilities require deliberate command/tool design
instead of arbitrary project-file generation; that friction is intentional.

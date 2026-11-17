# 0001. Record architecture decisions

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

`fit` produces numbers that people trade and report on. Many of its design choices are not
visible in the code: why a curve stores continuously compounded zero rates, why key-rate risk is
computed by bumping zero rates rather than market quotes, why yields follow street convention.
When such choices are undocumented, they get re-litigated in code review, changed by accident,
or — worst — changed silently so that a number moves and nobody can say why.

The project needs a lightweight, durable record of significant decisions that lives with the
code, is reviewed like code, and survives contributor turnover.

## Decision

We will record architecturally significant decisions as Architecture Decision Records in
`docs/adr/`, one Markdown file per decision, numbered sequentially (`NNNN-kebab-title.md`).

Each record has the sections Status, Context, Decision, Consequences and Alternatives
considered, following [template.md](template.md). The criteria for when an ADR is required, the
review process and the status lifecycle are defined in the [ADR README](README.md).

Accepted ADRs are immutable. A decision is changed by a new ADR that supersedes the old one.

## Consequences

- Reviewers can point to a record instead of re-arguing a settled question, and can require an
  ADR when a PR changes methodology or contracts.
- Users and model validators have a traceable rationale for every methodology choice.
- Contributors carry a small documentation cost on significant changes.
- The index must be kept in step with the files; this is checked in review.

## Alternatives considered

- **Wiki or external design documents.** Drift from the code, are not versioned with it and are
  not subject to review. Rejected.
- **Commit messages and PR descriptions only.** Hard to discover, tied to a hosting platform and
  describe changes rather than standing decisions. Rejected as the sole record.
- **A single design document.** Becomes long, loses the history of what was decided when, and
  encourages in-place rewriting of past rationale. Rejected.

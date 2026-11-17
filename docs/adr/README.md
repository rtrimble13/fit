# Architecture Decision Records

This directory holds the Architecture Decision Records (ADRs) for `fit`. An ADR captures one
significant decision: the context that forced it, what was decided, and what follows from it.
ADRs are the authoritative explanation of *why* the code looks the way it does; the code and
[METHODOLOGY.md](../METHODOLOGY.md) describe *what* it does.

## Index

| ADR | Title | Status |
|-----|-------|--------|
| [0001](0001-record-architecture-decisions.md) | Record architecture decisions | Accepted |
| [0002](0002-cpp20-cmake-toolchain.md) | C++20, CMake and Ninja toolchain | Accepted |
| [0003](0003-zero-runtime-dependencies.md) | Zero third-party runtime dependencies | Accepted |
| [0004](0004-layered-library-plus-thin-cli.md) | Layered core library plus thin CLI | Accepted |
| [0005](0005-units-and-conventions-at-cli-boundary.md) | Units and conventions at the CLI boundary | Accepted |
| [0006](0006-double-precision-numerics.md) | IEEE double-precision numerics | Accepted |
| [0007](0007-curve-representation-and-interpolation.md) | Curve representation and interpolation | Accepted |
| [0008](0008-iterative-bootstrap.md) | Iterative curve bootstrap | Accepted |
| [0009](0009-risk-by-bump-and-reprice.md) | Curve risk by bump-and-reprice | Accepted |
| [0010](0010-csv-inputs-and-table-csv-json-outputs.md) | CSV inputs; table, CSV and JSON outputs | Accepted |
| [0011](0011-error-handling-and-exit-codes.md) | Error handling and exit codes | Accepted |
| [0012](0012-testing-and-numerical-validation.md) | Testing and numerical validation | Accepted |
| [0013](0013-horizon-analysis-methodology.md) | Horizon analysis methodology | Accepted |
| [0014](0014-monotone-convex-interpolation.md) | Monotone convex interpolation | Proposed |
| [0015](0015-selectable-extrapolation.md) | Selectable extrapolation beyond the last pillar | Proposed |
| [0016](0016-multi-curve-construction.md) | Multi-curve construction: projection curves on a discount curve | Proposed |

## When an ADR is required

Write an ADR when a change does any of the following:

- alters a numerical methodology or market convention (interpolation, bootstrap, yield or
  spread definition, risk measure definition, P&L decomposition);
- adds or removes a dependency, or changes the supported toolchain;
- changes the module layering or a public interface contract of `fit_core`;
- changes the CLI contract: command structure, units, input schemas, output schemas, exit codes;
- reverses or materially amends an existing ADR.

Bug fixes, new tests, refactors that preserve behaviour, and additions that follow an existing
pattern (for example a new day count following the documented checklist) do not need an ADR.

## Process

1. Copy [template.md](template.md) to `NNNN-short-kebab-title.md`, using the next free number.
2. Fill it in with status `Proposed`. Keep it short: one decision per record.
3. Open a pull request containing the ADR, ideally together with (or ahead of) the
   implementation. Discussion happens in review.
4. On merge the status becomes `Accepted` and the index above is updated in the same PR.

## Lifecycle

- **Proposed** — under review, not yet binding.
- **Accepted** — in force.
- **Deprecated** — no longer relevant; kept for history.
- **Superseded by NNNN** — replaced by a later ADR.

Accepted ADRs are immutable apart from status changes and typo fixes. To change a decision,
write a new ADR that supersedes the old one and link the two in both directions.

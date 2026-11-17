# 0004. Layered core library plus thin CLI

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

The command-line interface is the first consumer of the analytics but will not be the last:
Python bindings and a batch/server mode are on the roadmap. Analytics that are entangled with
argument parsing and output formatting cannot be unit-tested precisely or reused. Equally, a
library without layering rules tends towards cyclic dependencies between curves, instruments and
portfolios.

## Decision

All financial logic lives in the static library `fit_core`; the `fit` executable is a thin
adapter over it.

**Library modules.** Public headers are in `include/fit/<module>/`, implementations in
`src/<module>/`. A module may depend only on modules listed before it:

| Module | Responsibility |
|--------|----------------|
| `core` | `fit::Error` and other foundation types |
| `time` | dates, tenors, calendars, business-day conventions, day counts, schedules |
| `math` | interpolation and root-finding |
| `curve` | yield-curve representation, derived curves (shifted, forward-implied), bootstrap |
| `bond` | bond definition, cash flows, yield/price/risk analytics, horizon analysis |
| `portfolio` | positions, valuation, aggregation, scenarios, pre-trade comparison |
| `io` | CSV reading; table, CSV and JSON rendering |

`io` is a leaf utility: it depends only on `core` and may be used by `portfolio` loaders and by
the CLI. Pricing modules never print.

**CLI.** `src/cli/` contains argument parsing, one translation unit per command group and
`main`. A command does exactly four things: parse and validate arguments, convert boundary units
to internal units ([ADR 0005](0005-units-and-conventions-at-cli-boundary.md)), call `fit_core`,
and render the result. It contains no financial mathematics.

**Namespace.** Everything is in namespace `fit`.

## Consequences

- Analytics are testable directly at full precision, without spawning a process or parsing
  formatted text.
- Future front ends link `fit_core` and inherit identical numbers.
- The dependency order is explicit and can be enforced in review; a `bond` header including a
  `portfolio` header is a defect.
- Some result structures exist purely to carry values from the library to the renderer, which is
  a small amount of duplication.
- The public header set is a de facto API. In the 0.x series it may change between minor
  versions, but changes must be deliberate and noted in the changelog.

## Alternatives considered

- **Single executable with logic in the command handlers.** Fastest to write; untestable at the
  unit level and not reusable. Rejected.
- **Header-only library.** Simplifies consumption but lengthens builds and exposes
  implementation detail. Rejected.
- **One library per module.** Enforces layering at link time but adds build complexity out of
  proportion to the size of the code base. May be revisited if the library grows substantially.

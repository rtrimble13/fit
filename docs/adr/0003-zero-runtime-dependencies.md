# 0003. Zero third-party runtime dependencies

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

A pricing tool on a trading desk is subject to software approval, vulnerability scanning and
model validation. Every third-party library is something to approve, patch, license-check and
explain. A general-purpose quant library would also bring its own conventions and defaults,
which then become part of our numbers without being decisions we made.

The functionality v0.1 needs outside the financial mathematics is modest: argument parsing, CSV
reading, and rendering tables, CSV and JSON.

## Decision

`fit_core` and the `fit` binary depend only on the C++ standard library.

- Argument parsing, CSV reading, and table/CSV/JSON writing are implemented in-tree (`src/cli`
  and the `io` module).
- Dates, calendars, day counts, schedules, interpolation, root-finding, bootstrapping and
  analytics are implemented in-tree.
- The single exception is test-only: doctest 2.4.11 is vendored as a single header at
  `third_party/doctest/doctest.h`. It is not linked into `fit_core` or `fit`.
- The build performs no network access: no `FetchContent`, no package manager.

Adding any dependency — runtime, build-time or test-time — requires a new ADR.

## Consequences

- The binary is self-contained and the software bill of materials is the standard library.
- Every number the tool prints is produced by code in this repository, which simplifies model
  validation and debugging.
- We own code that mature libraries already provide. The in-tree CSV reader, JSON writer and
  argument parser are deliberately minimal (for example, JSON is write-only), and they need
  their own tests.
- Correctness of financial primitives cannot be delegated to a library. This is mitigated by
  cross-validation against QuantLib and published reference values during development
  ([ADR 0012](0012-testing-and-numerical-validation.md)); QuantLib is a validation reference,
  not a dependency.
- Features such as richer input formats will cost more to add than they would with libraries.

## Alternatives considered

- **Link QuantLib.** Comprehensive and well tested, but large, Boost-dependent and opinionated
  about architecture. It would make `fit` a front end rather than a toolkit. Rejected as a
  dependency; retained as an independent check.
- **Small header-only libraries (CLI11, nlohmann/json, fmt).** Individually reasonable. Rejected
  for v0.1 because the needed subset is small and each addition carries a recurring approval and
  upgrade cost. May be revisited by ADR if in-tree code becomes a maintenance burden.
- **Fetch the test framework at configure time.** Breaks offline and hermetic builds. Rejected
  in favour of vendoring one header.

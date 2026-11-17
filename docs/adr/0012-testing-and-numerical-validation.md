# 0012. Testing and numerical validation

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

Numerical code can be self-consistent and wrong. A test whose expected value was produced by the
code under test proves only that the code has not changed. For a tool whose outputs inform
trades, correctness must be established against sources independent of the implementation, and
any later movement in a number must be noticed, explained and justified.

## Decision

**Layers of testing.**

1. **Unit tests** (`tests/unit/*.cpp`, doctest) exercise `fit_core` directly, at full precision.
2. **Property and identity tests** assert relationships that must hold regardless of inputs:
   a bootstrapped curve reprices its instruments; price-from-yield and yield-from-price
   round-trip; key-rate DV01s sum to parallel DV01; a horizon P&L decomposition sums to the
   total; a bond priced at its own z-spread returns its market price.
3. **CLI integration tests** (`tests/cli/`) run the built `fit` binary and check exit codes,
   stream discipline and output across formats.

All layers run through `ctest`, in the `dev` preset with AddressSanitizer and
UndefinedBehaviorSanitizer enabled, on both supported compilers in CI.

**Golden values.** Expected values for conventions and analytics come from an independent
source, cited in a comment next to the value:

- published reference values — the ISDA day-count examples and the documented examples of the
  Excel functions `PRICE`, `YIELD`, `DURATION` and `MDURATION`;
- QuantLib, used during development as an independent implementation; or
- a closed-form or hand calculation shown in the comment.

QuantLib is a development-time reference only. It is not a build or test dependency
([ADR 0003](0003-zero-runtime-dependencies.md)); the values it produced are recorded in the
tests.

**Rules.**

- Every change to pricing, curve or risk logic includes a test with an independently sourced
  expected value and an explicitly stated tolerance.
- Tolerances are chosen per quantity and justified by the precision of the source; they are not
  loosened to make a test pass.
- A change to an existing golden value is a numerical change. It must be explained, and the new
  value independently validated, under the policy in
  [PR_STANDARDS.md](../PR_STANDARDS.md).
- A defect fix includes a test that fails without the fix.

## Consequences

- The suite demonstrates agreement with external references, not merely stability.
- Unintended numerical changes surface as test failures, and intended ones leave an audit trail.
- Writing a test costs more: an independent value has to be obtained and cited.
- Where an independent implementation uses a different convention, the difference must be
  understood and documented rather than absorbed into a tolerance.
- Tests are fast and hermetic: no network, no external processes other than the `fit` binary in
  integration tests.

## Alternatives considered

- **Snapshot tests of the tool's own output as the main strategy.** Detect change, not error.
  Used only as a supplement in integration tests.
- **Linking QuantLib into the test suite for live comparison.** Strong, but imports a heavy
  dependency into the build and CI. Rejected in favour of recorded values.
- **GoogleTest or Catch2.** Both suitable; doctest was chosen for being a single small header
  with fast compile times, which suits vendoring.
- **Coverage thresholds as a gate.** Coverage measures execution, not correctness. Not adopted
  as a gate in v0.1.

# 0006. IEEE double-precision numerics

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

Financial software sometimes uses decimal arithmetic so that sums of money are exact. `fit`,
however, is an analytics tool, not a ledger: its work is dominated by exponentials, logarithms,
powers, interpolation and iterative root-finding, none of which are exact in any finite
representation. The requirement is that results are accurate well beyond quoting precision,
reproducible, and fast.

## Decision

All numerical quantities in `fit_core` are IEEE 754 binary64 (`double`). There is no decimal or
fixed-point type.

- **Dates** are integer serial day numbers; date arithmetic is exact.
- **Tolerances** are explicit named constants, never inline literals. Root-finding (yield,
  z-spread, bootstrap) converges to tolerances several orders of magnitude tighter than any
  displayed precision.
- **Rounding** happens only at output, controlled by `--precision`. The library never rounds
  intermediate or returned values. Settlement-amount rounding to currency minor units is the
  responsibility of downstream booking systems.
- **Comparisons** in tests state an absolute or relative tolerance appropriate to the quantity;
  exact floating-point equality is used only where the value is exact by construction.
- **Compiler flags** that relax IEEE semantics (`-ffast-math` and equivalents) are prohibited,
  so results do not depend on optimisation level in ways that violate the standard.
- Non-finite values (NaN, infinity) are never valid results; inputs that would produce them are
  rejected with `fit::Error`.

## Consequences

- With roughly 15–16 significant digits, a price per 100 is accurate to far better than
  1e-10 and a market value on a billion of face to well under a cent, before aggregation.
- Analytics are fast and use standard library mathematics directly.
- Displayed totals may differ from the sum of displayed rows in the last printed digit, because
  rows are rounded for display and totals are computed from unrounded values. This is expected
  and documented.
- Results are reproducible on a given platform and toolchain; across compilers and standard
  libraries, differences at the level of the last few bits of `exp`/`log`/`pow` are possible.
  Golden-value tests therefore use tolerances rather than bitwise comparison.
- `fit` must not be used as the system of record for cash amounts.

## Alternatives considered

- **Decimal floating point or a decimal library.** Exact for sums of decimal amounts, but offers
  nothing for transcendental functions, is much slower, and would need a third-party library
  (contrary to [ADR 0003](0003-zero-runtime-dependencies.md)). Rejected.
- **`long double`.** Width and semantics vary by platform, harming reproducibility for no
  practical gain. Rejected.
- **Fixed-point money type for monetary outputs only.** Adds conversion points and a false sense
  of exactness for values derived from `double` prices. Rejected for v0.1.

# 0016. Multi-curve construction: projection curves on a discount curve

- **Status:** Proposed
- **Date:** 2026-11-17

## Context

Until now one curve has served both to project floating rates and to discount
([ADR 0008](0008-iterative-bootstrap.md)). That is right for an overnight-indexed (OIS) curve,
whose floating leg is the discounting rate itself. Since collateralised swaps moved to OIS
discounting, a term rate such as 3M Term SOFR or 6M EURIBOR is no longer its own discount
rate. A swap on that rate fixes the forwards of a *projection* curve, while its cash flows are
discounted on the OIS curve. Building such a curve on its own swaps as a single curve gives
wrong forwards, and the basis to OIS disappears into the discount factors. Floating-rate notes
(roadmap v0.3) need projection curves, and so does any forward-rate analysis of a term index.

Two decisions follow. One is how a projection curve's instruments are priced. The other is
how a user says which curve discounts which.

## Decision

**Roles.** A curve either discounts itself (a single curve, as before) or is a projection
curve bootstrapped on a given, already built discount curve. In the library,
`BootstrapOptions::discount` holds that curve; when it is empty the curve discounts itself.
Both curves must have the same as-of date. Building the projection curve leaves the discount
curve unchanged: the two are built in sequence, not jointly.

**Pricing on a projection curve `P` with discount curve `D`:**

| Quote | Model rate |
|-------|------------|
| `deposit` | the forward of `P` over the deposit's period: a term-rate fixing is a deposit on `P` |
| `zero` | the zero rate of `P` |
| `swap` (OIS) | fixed leg discounted on `D`; floating coupons `D(e_j) (P(s_j)/P(e_j) - 1)` over the fixed periods, because overnight compounding telescopes on `P` |
| `irs` (new) | fixed leg discounted on `D`; floating coupons `D(e_j) (P(s_j)/P(e_j) - 1)` over the floating periods |
| `par` | rejected: a bond quote prices on the curve itself, so it means nothing on a curve discounted elsewhere |

Fixed and floating periods both accrue between modified-following adjusted dates and are paid
at their adjusted end, with no payment lag. Floating coupons are *par coupons*: the forward is
taken over the accrual period itself, so the floating day count cancels and needs no input.
When `P` is `D`, the floating legs telescope to `D(S) - D(E)`, and every formula reduces
exactly to the single-curve one.

**The `irs` quote** is a fixed vs term-rate swap. Its defaults are an annual ACT/360 fixed leg
against quarterly floating periods (a 3M index). The `daycount` and `frequency` columns override
the fixed leg, and a new optional `float_frequency` column overrides the floating periods.
`float_frequency` is rejected on other quote types.

**CLI, explicit pair.** `--discount-curve FILE --projection-curve FILE` builds the discount
curve, then the projection curve on it, from the same `--asof`, `--interp`,
`--extrapolation`, `--calendar`, `--holidays` and `--spot-lag`.

- `fit curve query` and `fit curve forwards` report the projection curve.
- `fit curve build` reports both curves. It shows a `curves` table with one row per curve, a
  `market` table, and one `nodes` table whose first column names the curve. The curves are
  named `discount` and `projection` (layout in [ADR 0017](0017-curve-sets.md)).
- Bond, portfolio and pre-trade commands discount every bond on the discount curve. Fixed-rate
  bonds have no use for a projection curve.

`--curve` keeps its meaning and output. It cannot be combined with the pair, and each option of
the pair requires the other. A curve-set file naming any number of curves is the subject of a
separate decision ([ADR 0017](0017-curve-sets.md)).

## Consequences

- Projected forwards carry the basis between the term rate and OIS, and the projection curve
  reprices its swaps under OIS discounting. For flat-forward and linear-zero interpolation the
  results match QuantLib's dual-curve bootstrap (`SwapRateHelper` with an exogenous
  discounting curve and par coupons) to 2e-12.
- Single-curve inputs and outputs are unchanged. The discount curve of a pair is built exactly
  as it would be with `--curve`.
- Sequential building is exact here, since the discount curve's instruments do not depend on
  the projection curve. A joint solve would be needed only for instruments that tie the two
  together, such as basis swaps, which are left for the "additional instruments" roadmap item.
- Risk commands bump the discount curve, which is the only one fixed-rate bonds depend on.
  Sensitivities to projection curves are needed only once floating-rate instruments exist
  (v0.3), and their definition (curves held or rebuilt) will be decided then.
- Par coupons take the forward over the coupon's accrual period rather than over the index's
  own fixing period, as QuantLib does by default. On the example 3M curve, indexed coupons
  would move projection discount factors by up to 8e-6 and 3M forwards by up to 0.3bp.
  Indexed coupons and payment lags are not modelled.
- `par_sa_pct` in `fit curve query` stays a property of the queried curve's own discount
  factors. On a projection curve it has no market meaning.

## Alternatives considered

- **Joint (global) solve of both curves.** Unnecessary with these instruments, slower, and it
  fails less transparently ([ADR 0008](0008-iterative-bootstrap.md)). Rejected for now.
- **Indexed coupons**, with the forward over the index's own fixing period. They are more exact
  for unusual stubs, but need fixing-date and index-tenor conventions per curve and add no
  accuracy on regular schedules. Rejected until there is a user.
- **Allowing `par` quotes on a projection curve.** A bond discounted on its own projection curve
  is not an instrument anyone trades, so accepting it would only hide input mistakes. Rejected.
- **Inferring roles from quote types**, for example treating any file with `irs` quotes as a
  projection curve. That is implicit and fragile, and an OIS curve can legitimately contain
  `irs` quotes. Rejected in favour of naming the discount curve explicitly.

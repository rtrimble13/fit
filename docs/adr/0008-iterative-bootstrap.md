# 0008. Iterative curve bootstrap

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

Curves are built from market quotes, not typed in as zero rates. The build must reprice its
inputs exactly, work with every interpolation scheme in
[ADR 0007](0007-curve-representation-and-interpolation.md) — including the global natural
spline, where changing one node changes the curve everywhere — and make its quality visible so a
user can trust or reject the result.

## Decision

**Inputs.** A quotes file with one instrument per row. Supported quote types in v0.1:

| Type | Meaning | Repricing condition |
|------|---------|---------------------|
| `deposit` | simple-interest money-market rate to maturity | accrual at the quoted rate discounts back to par |
| `swap` | par rate of a single-curve, OIS-style swap with annual fixed payments | fixed leg PV equals `DF(start) - DF(end)` |
| `par` | par yield of a coupon bond | a bond with coupon equal to the quote prices at par |
| `zero` | continuously compounded zero rate | node value equals the quote |

Each instrument contributes one pillar at its maturity date; the unknown is the zero rate at
that pillar. Two instruments with the same pillar date are an error.

**Algorithm.**

1. Sort instruments by maturity and initialise node values from the quotes.
2. Sweep the pillars in maturity order. For each pillar, solve with Brent's method for the node
   value that reprices its instrument, holding all other nodes at their current values.
3. Repeat the sweep until every instrument reprices within tolerance, up to a fixed maximum
   number of passes.
4. Report the repricing residual for every instrument. Failure to converge, or failure to
   bracket a root, raises `fit::Error` naming the instrument; a curve that does not reprice is
   never returned silently.

For local schemes (`flat-forward`, `linear-zero`) the first sweep is already exact and the
second merely confirms it. For `cubic-zero` and `monotone-cubic` later nodes influence earlier
segments, and the repeated sweeps converge to the simultaneous solution.

**Scope.** The curve is a single curve used for both projection and discounting.

## Consequences

- One algorithm serves all interpolation schemes and all quote types; a new instrument type only
  needs a repricing function.
- Brent's method is derivative-free and robust, so instruments need no analytic sensitivities.
- Residuals are part of the output, giving users and tests a direct, quantitative check on the
  build.
- Build cost is passes × pillars × solver iterations × instrument valuation. For desk-sized
  curves (tens of instruments) this is negligible; it is not optimised for thousands of rebuilds
  per second.
- An exact bootstrap fits every quote, including noisy ones; it is not a smoothing fit.
  Overlapping or inconsistent instruments are the user's responsibility to curate.
- Single-curve construction is not appropriate for instruments that need separate projection and
  discount curves; multi-curve construction is on the roadmap.

## Alternatives considered

- **Single-pass sequential bootstrap only.** Exact for local interpolation but wrong for global
  splines. Rejected because it would silently restrict or mis-build `cubic-zero`.
- **Global multi-dimensional solve (Newton or Levenberg–Marquardt on all nodes).** Fast
  convergence, but needs a Jacobian and more machinery, and fails less transparently. May be
  revisited with multi-curve work.
- **Parametric least-squares fit (Nelson–Siegel–Svensson).** A different product: smooth but not
  exact. Planned as a complementary feature, not a replacement.
- **Closed-form bootstrapping per instrument type.** Possible for some instrument/interpolation
  pairs, but multiplies special cases. Rejected in favour of a uniform solver.

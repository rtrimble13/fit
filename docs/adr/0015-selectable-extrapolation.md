# 0015. Selectable extrapolation beyond the last pillar

- **Status:** Proposed
- **Date:** 2026-11-17

## Context

[ADR 0007](0007-curve-representation-and-interpolation.md) fixes extrapolation per scheme.

- The zero-rate schemes (`linear-zero`, `cubic-zero`) hold the zero rate flat beyond the last
  pillar.
- The schemes on `-ln DF` (`flat-forward`, `monotone-cubic`, and `monotone-convex` from
  [ADR 0014](0014-monotone-convex-interpolation.md)) hold the last forward rate flat.

A user can therefore change the extrapolation only by changing the interpolation, which is the
wrong trade-off. Discount factors beyond the last pillar are used for every cash flow past the
last quote, in bond, portfolio and pre-trade valuation and risk, and for any curve query beyond
the curve. Horizon analysis of a bond maturing inside the curve does not use them: its
forward-implied and rebased curves read only up to the bond's maturity.

The two rules can differ substantially there. On the example Treasury curve under
`linear-zero`, the 50-year discount factor is 0.1004 with the zero rate held flat and 0.1079
with the last forward held flat. Other systems also differ in their default: QuantLib's
interpolated curves hold the instantaneous forward at the last pillar.

Before the first pillar the question does not arise in the same way. The log-discount schemes
interpolate from an anchor at `(0, 0)`. For the zero-rate schemes, holding the zero rate flat
back to `t = 0` is the same as a constant forward equal to that rate. Any other "flat-forward"
rule there would stop the curve from returning `DF(0) = 1` or from reproducing the first node.

## Decision

We will make extrapolation beyond the last pillar selectable, with two rules.

- **`flat-forward`.** The instantaneous forward at the last pillar `t_n`, taken from inside the
  curve, is held beyond it: `-ln DF(t) = z_n t_n + f_n (t - t_n)`. On the `-ln DF` schemes,
  `f_n` is the slope of the interpolant at `t_n`. On the zero-rate schemes it is
  `f_n = z_n + t_n z'(t_n)`, with `z'` the slope of the zero interpolant at `t_n`. This is the
  rule QuantLib's `InterpolatedZeroCurve` applies. Forwards stay continuous at the last pillar.
- **`flat-zero`.** The zero rate at the last pillar is held beyond it: `DF(t) = exp(-z_n t)`.
  The forward jumps at `t_n` from `f_n` to `z_n`, unless the two are equal.

The library type is `fit::Extrapolation`. `InterpolatedCurve` and the bootstrap take it as an
explicit parameter, and the CLI exposes it as `--extrapolation flat-forward|flat-zero`.

- **Default.** When the option is not given, each scheme keeps its ADR 0007 extrapolation:
  `flat-zero` for `linear-zero` and `cubic-zero`, `flat-forward` otherwise. The library names
  this rule `natural_extrapolation`. No existing output changes.
- **Before the first pillar**, behaviour is unchanged and is not configurable, for the reasons
  above.
- **Bootstrap.** The option is carried through, but it cannot change the solved nodes, because
  no bootstrap instrument pays beyond its own pillar. A test asserts this.
- **Reporting.** `fit curve build` reports the extrapolation in its summary, next to the
  interpolation, so every curve states the rule that produced its long end.

## Consequences

- Extrapolation and interpolation can be chosen independently. Users can match another system's
  long end, or test how much a valuation depends on the rule.
- With the default, results are unchanged for every existing input.
- The curve summary has one more row. This is an additive change to the output schema. The
  documented examples were regenerated.
- `flat-forward` on a zero-rate scheme inherits the slope of the last interpolated segment. A
  steep final segment therefore gives an extreme long-end forward. That is the meaning of the
  rule, and it is documented rather than damped.
- Both rules remain simple. Ultimate-forward-rate methods such as Smith–Wilson are not part of
  this decision.

## Alternatives considered

- **Change the default to `flat-forward` for every scheme.** That would give one consistent rule
  and match QuantLib, but it changes existing outputs under `linear-zero` and `cubic-zero`, which
  is a numerical change and a contract change. It can be proposed separately. Rejected here.
- **Use the last discrete forward between the last two pillars for `flat-forward`.** That is
  QuantLib's rule for its forward curves. On `flat-forward` interpolation it coincides with the
  chosen rule, but on the other schemes it puts a jump in the forward at the last pillar.
  Rejected in favour of continuity.
- **Make the short end configurable too.** There is no rule for the region before the first
  pillar that keeps `DF(0) = 1` and reproduces the first node other than the present one.
  Rejected.
- **Extrapolation to an ultimate forward rate** (Smith–Wilson, or a convergence period). This is
  a different feature with its own parameters and no current user. Not adopted.

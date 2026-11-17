# 0007. Curve representation and interpolation

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

Every valuation, risk and horizon calculation reads discount factors from a yield curve. The
curve's internal representation determines which interpolation schemes are natural, how
shifts and spreads are applied, and how forward rates behave between pillars. Interpolation is a
genuine modelling choice: schemes trade smoothness against locality and against the risk of
oscillating or negative forwards. Desk users need to choose and to compare.

## Decision

**Representation.** A curve is defined by an as-of date and a set of nodes
`(t_i, z_i)`, where:

- `t` is the time from the as-of date in years under ACT/365 Fixed, for every curve regardless of
  the conventions of the instruments used to build it;
- `z` is the continuously compounded zero rate to `t`, so `DF(t) = exp(-z(t) * t)`.

The fundamental query is the discount factor at a time or date. Zero rates in other compounding
conventions, forward rates between two dates and instantaneous forwards are derived from
discount factors.

**Interpolation schemes** (selectable; the name is the CLI value):

| Scheme | Definition | Character |
|--------|------------|-----------|
| `flat-forward` (default) | linear in `ln DF` between nodes | piecewise-constant forwards; local; forwards positive whenever discount factors decrease; forwards jump at nodes |
| `linear-zero` | linear in `z` | simple and local; forwards are discontinuous and can be distorted near nodes |
| `cubic-zero` | natural cubic spline through `z` | smooth zeros and continuous forwards; global, so a move in one quote affects the whole curve; can oscillate |
| `monotone-cubic` | PCHIP (Fritsch–Carlson) on `-ln DF` | smooth and local; preserves monotonicity of `-ln DF`, so forwards stay non-negative when the inputs imply it |

**Extrapolation.** Each scheme extrapolates in the quantity it interpolates:

- `linear-zero` and `cubic-zero` hold the zero rate flat at the nearest node on both sides;
- `flat-forward` and `monotone-cubic` interpolate `-ln DF` from an anchor at `(0, 0)`. Beyond
  the last node they continue with the last forward rate (flat-forward extrapolation). Before
  the first node `flat-forward` is equivalent to a flat zero rate, while `monotone-cubic`
  follows its spline down to `t = 0`, so the forward curve stays smooth at the short end.

**Derived curves.** Scenario and risk curves are built by composition rather than by editing
nodes: a shifted curve adds a spread function of time to the zero rate (parallel, or triangular
for key rates), and a forward-implied curve re-bases discount factors to a horizon date. Any
curve implementation therefore only has to supply discount factors.

Hagan–West monotone convex interpolation and Nelson–Siegel–Svensson parametric fitting are
out of scope for v0.1 and are on the [roadmap](../ROADMAP.md).

## Consequences

- One time axis and one node quantity make curves from different instrument sets directly
  comparable and make zero-rate shifts trivial and exact.
- Negative rates need no special handling in the representation.
- `flat-forward` as the default is robust and never surprises on repricing, at the cost of
  discontinuous forwards; users wanting smooth forwards select a cubic scheme explicitly.
- `cubic-zero` is global, which is why the bootstrap must iterate
  ([ADR 0008](0008-iterative-bootstrap.md)) and why key-rate risk is defined on zero-rate bumps
  rather than through interpolation locality ([ADR 0009](0009-risk-by-bump-and-reprice.md)).
- `monotone-cubic` guarantees non-negative forwards only where `-ln DF` is itself
  non-decreasing; in negative-rate regimes it remains a valid interpolant without that
  guarantee.
- Extrapolation is a simplification under every scheme; valuations depending on cash flows beyond the
  last pillar should be treated with care, and the tool's documentation says so.
- ACT/365 Fixed curve time means a "1Y" node is not exactly `t = 1` across leap years; this is
  intended and consistent.

## Alternatives considered

- **Store discount factors at nodes.** Equivalent information; zero rates are easier to inspect,
  shift and interpolate, and avoid a degenerate node at `t = 0`.
- **Store instantaneous forwards.** Natural for some schemes but makes discount factors an
  integral and complicates simple interpolation. Rejected.
- **A single hard-wired interpolation.** Simpler, but interpolation sensitivity is something a
  desk needs to examine. Rejected.
- **Monotone convex as the default in v0.1.** The preferred long-term default for forward
  quality, but intricate to implement and validate correctly. Deferred to v0.2 rather than
  shipped under-tested.
- **ACT/ACT or instrument-specific curve time.** Adds complexity and makes curves built from
  mixed instruments inconsistent. Rejected.

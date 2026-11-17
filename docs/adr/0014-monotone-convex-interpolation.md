# 0014. Monotone convex interpolation

- **Status:** Proposed
- **Date:** 2026-11-17

## Context

[ADR 0007](0007-curve-representation-and-interpolation.md) deferred Hagan–West monotone convex
interpolation to v0.2, calling it the preferred long-term default for forward quality. Each
v0.1 scheme compromises the forward curve:

- `flat-forward` forwards jump at every pillar;
- `linear-zero` forwards are discontinuous and can saw-tooth;
- `cubic-zero` is global and can oscillate into negative forwards;
- `monotone-cubic` makes forwards continuous, and non-negative while `-ln DF` increases. It
  constrains discount factors, though, not forwards, so forwards can still overshoot the
  discrete forwards between pillars.

Hagan and West's method ("Interpolation methods for curve construction", *Applied Mathematical
Finance* 13(2), 2006; "Methods for constructing a yield curve", *Wilmott Magazine*, May 2008)
is built on the forwards themselves. Each interval's instantaneous forward is a piecewise
quadratic that averages exactly to the discrete forward between pillars. The forward is
continuous and does not overshoot where the discrete forwards are monotone. An optional bound on
the node forwards keeps it positive.

The published method leaves choices that change numbers:

- **Positivity.** Each node forward is bounded to `[0, 2 min(adjacent discrete forwards)]`.
  That bound has no meaning when a discrete forward is negative, because its upper end falls
  below zero.
- **The ends.** It must be stated how the curve behaves before the first pillar and after the
  last.
- **QuantLib's version.** QuantLib's `ConvexMonotone` adds two blending parameters,
  `quadraticity` and `monotonicity`. Their defaults (0.3 and 0.7) move the shape away from the
  published method. Its `forcePositive` switch also works differently from the published bound.

## Decision

We will add a fifth interpolation scheme, `monotone-convex`. The algorithm is implemented in
`math` as `fit::MonotoneConvex`, and users select the scheme with `--interp monotone-convex`.
The default remains `flat-forward`. This ADR adds to the schemes listed in ADR 0007 and changes
none of them.

- **Representation.** Unchanged from ADR 0007. Like `monotone-cubic`, the scheme interpolates
  `-ln DF(t)` through `(0, 0)` and the pillars `(t_i, z_i t_i)`. The secants of `-ln DF` are the
  discrete forwards `fd_i`, and the derivative of the interpolant is the instantaneous forward.
- **Method.** The published, unblended construction:
  - Interior node forwards come from linear interpolation of the discrete forwards placed at
    their interval midpoints:
    `f_i = (h_i fd_(i+1) + h_(i+1) fd_i) / (h_i + h_(i+1))`.
  - The end values are `f_0 = fd_1 - (f_1 - fd_1) / 2` and `f_n = fd_n - (f_(n-1) - fd_n) / 2`.
    They are computed from the interior estimates before any bound is applied, so that
    bounding an interior node does not also move the end values.
  - On each interval, `g = f - fd` follows the four-region construction, with region (i)
    including its boundaries.
- **Positivity.** A node forward is bounded to `[0, 2 min(adjacent discrete forwards)]` when
  those discrete forwards are non-negative, and left as estimated otherwise. This gives:
  - on a curve whose discrete forwards are non-negative, the published positive variant;
  - on a curve whose discrete forwards are all negative, the published unconstrained method;
  - on a mixed curve, the same choice made node by node from the node's own neighbours.
- **Extrapolation.**
  - Before the first pillar, the anchor at `t = 0` makes `[0, t_1]` an interval like any other,
    so the method itself applies there.
  - After the last pillar, the instantaneous forward is held at its value at that pillar. This
    is ADR 0007's rule for the log-discount schemes, and it keeps forwards continuous.
- **Validation.** Expected values in tests come from QuantLib's `ConvexMonotone` with
  quadraticity 0, monotonicity 1 and `forcePositive` false. With those settings it is the
  published method, so the comparison applies where no positivity bound is active. Further
  expected values come from hand calculations of each region and of the bounds.
- **Default.** Unchanged. Making monotone convex the default would change existing outputs and
  the CLI contract. It is a separate decision, for a later ADR, after the scheme has been used
  on the validation corpus.

## Consequences

- Forwards are continuous at the pillars and stay positive on curves whose discrete forwards
  are positive. Where the discrete forwards are monotone, the forwards are monotone too. Forward
  rates between pillars are therefore better behaved than under any v0.1 scheme.
- The scheme is local but not strictly so. A pillar value moves the forward on at most two
  intervals on each side. The bootstrap still iterates
  ([ADR 0008](0008-iterative-bootstrap.md)), taking four passes on each example curve, and
  needs no change.
- When a discrete forward is a local extremum, the forward on its interval must overshoot it,
  since it has to average to that value while starting and ending on the same side. On positive
  curves the positivity bound limits how far.
- When two adjacent discrete forwards are exactly equal and the next one differs, the limit of
  the construction is a step in the forward at a pillar. Near-equal forwards give a
  correspondingly sharp change. Discount factors stay continuous in the inputs, so the bootstrap
  and bump-and-reprice risk are unaffected.
- Key-rate risk is still defined by zero-rate bumps
  ([ADR 0009](0009-risk-by-bump-and-reprice.md)). Through the node forwards, a bump also moves
  the neighbouring intervals.
- Results differ from QuantLib's `ConvexMonotone` at its default parameters. Beyond the last
  pillar they also differ from QuantLib's piecewise forward curve, which extrapolates the last
  discrete forward rather than the last instantaneous forward. To reconcile with QuantLib, use
  `ConvexMonotone(0, 1, false)` and compare inside the pillar range.

## Alternatives considered

- **QuantLib's blending parameters.** They have no concrete user, they depart from the published
  method, and exposing them would make them part of the CLI contract. Rejected.
- **Always apply the published bound.** It is undefined next to a negative discrete forward.
  Rejected.
- **Never apply the bound.** Ordinary curves would lose positivity: next to a sharp dip in the
  discrete forwards, the forward goes negative. Rejected.
- **A curve-wide switch** that bounds the curve only if every discrete forward is non-negative.
  A single negative forward anywhere would then change the curve everywhere. The node-by-node
  rule gives the same results on all-positive and all-negative curves and handles mixed curves
  locally. Rejected.
- **Make it the default now.** That is a numerical and contract change for every user, and it
  deserves its own decision once the scheme has been used. Deferred.

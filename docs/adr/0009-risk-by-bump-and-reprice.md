# 0009. Curve risk by bump-and-reprice

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

The tool reports two families of interest-rate risk. Yield-based measures — Macaulay and
modified duration, convexity and yield DV01 — have closed forms given a yield. Curve-based
measures — effective duration and convexity, curve DV01 and key-rate DV01s — depend on how the
curve is perturbed, and there are several defensible definitions. The definition must be
stated, uniform across instruments, additive across positions, and cheap enough to run on a
portfolio interactively.

## Decision

**Yield-based measures** are computed analytically from the street-convention yield.

**Curve-based measures** are computed by bump-and-reprice on the zero curve:

- **Bump space.** Perturbations are applied to continuously compounded zero rates by composing
  the base curve with a spread function of time
  ([ADR 0007](0007-curve-representation-and-interpolation.md)). Market quotes are not bumped and
  the curve is not re-bootstrapped.
- **Size and scheme.** 1 basis point, central difference:
  `DV01 = (PV(-1bp) - PV(+1bp)) / 2`, reported in currency units as the value gained for a 1bp
  fall in rates. Effective convexity uses the corresponding second difference.
- **Parallel.** The spread function is constant.
- **Key rates.** For a ladder of key tenors, the bump for key `k` is triangular: 1bp at tenor
  `k`, falling linearly to zero at the adjacent key tenors; the first and last keys extend flat
  to time zero and to infinity respectively. The bumps sum to a parallel 1bp shift at every
  point, so key-rate DV01s sum to the parallel curve DV01 up to second-order terms.
- **Spread.** A bond's z-spread is held constant under every bump.
- **Aggregation.** Portfolio risk is the sum of position risk; all measures are additive by
  construction.

## Consequences

- One mechanism covers every instrument that can be valued off a curve, including future
  instrument types, with no instrument-specific sensitivity code.
- Results are independent of the interpolation scheme's locality and of which instruments were
  used to build the curve: the same portfolio on a `cubic-zero` curve has a clean, local key-rate
  ladder.
- The ladder is in zero-rate space. It is not a ladder of sensitivities to the quoted par
  instruments, so it does not translate directly into hedge notionals in those instruments.
  Par-point (quote-bump, re-bootstrap) key-rate risk is on the roadmap.
- Cost is two revaluations per bump per position. This is ample for desk portfolios; algorithmic
  differentiation is a roadmap item should scale require it.
- A 1bp central difference has truncation error of order bump squared and is far from
  floating-point cancellation for prices near 100; the choice is a named constant.

## Alternatives considered

- **Bump quotes and re-bootstrap.** Gives hedge-instrument risk directly, but couples the ladder
  to the curve's instrument set and interpolation, and costs a bootstrap per bump. Deferred, not
  rejected: it will be added as an alternative mode.
- **Analytic curve sensitivities.** Exact and fast for fixed cash flows, but needs separate
  derivations for each instrument and curve type. Rejected for uniformity.
- **Algorithmic differentiation.** The right answer at scale; too much machinery for the MVP.
- **One-sided bumps.** Half the cost but first-order truncation error and convexity bias.
  Rejected.
- **Forward-rate (bucketed forward) bumps.** Useful for some desks, less familiar for bond
  portfolios. Not in v0.1.

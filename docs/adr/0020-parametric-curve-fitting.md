# 0020. Parametric curve fitting (Nelson-Siegel and Svensson)

- **Status:** Proposed
- **Date:** 2026-11-27

## Context

Every curve fit builds today is an exact bootstrap ([ADR 0008](0008-iterative-bootstrap.md)):
it reprices each quote and interpolates between pillars. That is right for swap curves, but
government bond curves are usually *fitted*. A smooth parametric curve is set through noisy or
inconsistent prices, and the residuals show which bonds are rich or cheap. Central banks
publish such curves; the Federal Reserve's is a Svensson fit (Gurkaynak, Sack and Wright,
2006). The M2 roadmap asks for Nelson-Siegel and Svensson least-squares fits with per-instrument
residuals and RMSE, as a complement to the bootstrap. Its exit criteria ask for validation
against independent implementations on a published or reconstructed data set.

Choices needed:

- the model's form;
- what is minimised;
- the optimiser and how it finds the global minimum;
- how a fitted curve reaches the commands.

## Decision

- **Models.** Nelson-Siegel and Svensson, as continuously compounded ACT/365F zero rates:
  `z(t) = b0 + b1 L(t/tau1) + b2 C(t/tau1) [+ b3 C(t/tau2)]`, with `L(x) = (1 - e^-x)/x` and
  `C(x) = L(x) - e^-x`. This is QuantLib's `NelsonSiegelFitting` and `SvenssonFitting` (with
  `kappa = 1/tau`), and the form the Federal Reserve publishes. The curve is defined at every
  maturity, so it has no interpolation and no extrapolation.
- **Objective.** The sum over quotes of (model-implied rate - quoted rate)^2, equally weighted,
  in each quote's own units. Implied rates are those of the bootstrap
  (`implied_quote_rate`), so every quote type can be fitted, on one curve or as a projection
  curve on a discount curve. Fitting bond prices directly, with a quote type for coupon bonds at
  a clean price and duration weights, is left for a later change.
- **Optimiser.** Levenberg-Marquardt with a central-difference Jacobian, over
  `(b0, b1, b2, [b3], ln tau1, [ln tau2])`, with each tau bounded to [0.05, 50] years. A
  parameter held at a bound is left out of the step that would push it further.
- **Global search.** The objective has local minima, mainly in tau. The fit starts from a fixed
  grid of taus, with `b0` at the longest quote, `b0 + b1` at the shortest and no curvature:
  - Nelson-Siegel: 0.5, 1, 2, 3, 5, 8 and 12 years.
  - Svensson: every ordered pair (tau1, tau2) of distinct values from 0.5, 1, 2, 4, 8 and 16
    years. Svensson's taus are not interchangeable (tau1 also shapes the slope), so both
    orders are tried and the result is reported as fitted.

  Each start runs 20 steps. The three lowest are then refined for up to 200 steps, and the
  lowest cost wins. The result is deterministic.
- **Command line.** `--fit nelson-siegel|svensson` on every command that builds a curve
  replaces the bootstrap. It cannot be combined with `--interp` or `--extrapolation`. Curve
  sets take a `fit` column (`nelson-siegel`, `svensson`, or `none` to bootstrap).
  - Every other command uses the fitted curve as it would a bootstrapped one: pricing, risk by
    bumping its zero rates, horizon analysis, portfolios and pre-trade.
  - `fit curve build` reports the parameters, the RMSE, the largest absolute residual and the
    iterations. Each quote's residual is shown in the nodes table's `error_bp`.
  - In a multi-curve market, a fitted curve's row in the `curves` table gives its model as
    the interpolation, no extrapolation and 0 bootstrap passes. A `fits` table gives its
    parameters.
- **Validation.**
  - The formula is checked against QuantLib's fitted curves.
  - The fit is checked against an independent one: SciPy `least_squares`, with the
    instruments priced by QuantLib.
  - Recovery: parameters are recovered from a reconstructed data set, zero yields generated
    from known Svensson parameters, exact and rounded to four decimals as the Federal Reserve
    publishes them.

## Consequences

- Bond curves can be fitted rather than bootstrapped, with residuals that show how rich or cheap
  each quote is against the fit. The fitted curve prices and risks like any other.
- A fitted curve does not reprice its inputs. Users must read the RMSE and residuals, which
  `fit curve build` reports.
- Svensson is weakly identified on few quotes: different parameters fit nearly equally well. On
  the example Treasury quotes, two independent optimisers agree on the RMSE to 5e-10bp and on
  each residual to 8e-5bp, but on the betas only to about 2e-5 percentage points. Parameters
  should be read with that in mind.
- The multi-start search costs tens of milliseconds for a Svensson fit of a dozen quotes,
  after each quote's schedules are resolved once. That change also made bootstraps about
  three times faster, with identical results.
- Key-rate risk bumps the fitted curve's zero rates, as for any curve. It does not refit to
  bumped quotes; par-point risk is roadmap M4.
- Bond price fitting and weighting schemes are not yet available.

## Alternatives considered

- **A separate `fit curve fit` command reporting only the fit.** It is smaller, but a fitted
  curve could then not price bonds. Rejected after discussion with the maintainer.
- **Fitting clean bond prices with duration weights (QuantLib's FittedBondDiscountCurve).**
  This is the standard for off-the-run Treasuries. It needs a new quote type and input schema,
  so it is deferred to its own change, as agreed.
- **Nelder-Mead simplex (QuantLib's default).** It needs no derivatives but converges slowly and
  less precisely on these narrow valleys. Levenberg-Marquardt suits a sum of squares.
- **Separable least squares** (solving the betas linearly for each tau). This is exact only for
  zero-rate quotes, since par yields and swap rates are not linear in the betas. Rejected for
  generality.
- **One start only.** Rejected: the treasury example and the reconstructed data sets both have
  local minima that a single start can fall into.

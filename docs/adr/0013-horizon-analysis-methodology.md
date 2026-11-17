# 0013. Horizon analysis methodology

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

Pre-trade analysis asks what a bond will earn over a holding period and why: how much is
coupon, how much is the bond rolling down the curve, how much depends on a view about rates.
"Forecasting" in `fit` means this deterministic horizon (scenario) analysis. The terms carry and
roll-down are used inconsistently across the market, so the tool needs one precise definition
whose components always sum to the total.

## Decision

**Set-up.** Given a bond, a curve as of today, a settlement date and a horizon date, the bond is
valued today (dirty price `P0`, at its market price if given, otherwise off the curve) and its
z-spread to today's curve is determined. That z-spread is held constant in every scenario.

**Horizon curve scenarios.**

| Scenario | Horizon curve | Interpretation |
|----------|---------------|----------------|
| `unchanged` | today's zero rate for each *tenor*, re-dated to the horizon | the curve does not move; the bond rolls down it |
| `forward` | discount factors implied by today's forwards, `DF_h(T) = DF_0(T) / DF_0(h)` | today's forward rates are realised |

Either scenario can be combined with one or more zero-rate shocks, in basis points, applied to
the horizon curve: a parallel shift, or a piecewise-linear twist given as tenor:bp nodes (for
example a 2s10s steepener).

**Cash flows in the period.** Coupons paid after settlement and on or before the horizon are
collected and reinvested to the horizon — at a user-supplied reinvestment rate if given,
otherwise at the forward rates implied by today's curve.

**Decomposition.** Per 100 face:

| Component | Definition |
|-----------|------------|
| Coupon income / accrual | coupons received plus the change in accrued interest |
| Roll-down | clean price at the horizon on the unshifted scenario curve, minus the clean price today |
| Curve shift | clean price at the horizon on the shifted curve, minus the clean price on the unshifted scenario curve |
| Reinvestment | interest earned on coupons between payment and the horizon |

The components sum exactly to total P&L:
`(dirty price at horizon + coupons + reinvestment income) - P0`.

**Returns.** Total return is total P&L divided by `P0`. The annualised return is reported
alongside it, with its compounding and day-count basis documented in
[METHODOLOGY.md](../METHODOLOGY.md).

## Consequences

- The decomposition is exact and additive, so position results aggregate to portfolio results
  without residuals.
- The definitions are explicit. In particular, "roll-down" here includes pull-to-par, and under
  the `forward` scenario it measures the price change when forwards are realised, which is not
  what some desks call roll. The scenario name is always shown with the result.
- Under `forward` with no shift, a bond's return approximates the curve's funding rate over the
  period plus its spread — a useful sanity check that is covered by tests.
- The analysis is deterministic: there is no rate distribution, no spread dynamics, no default
  and no financing leg. Net carry after repo funding is on the roadmap.
- Shocks are deterministic zero-rate shifts typed on the command line; a named scenario library
  and arbitrary user-supplied horizon curves are roadmap items.

## Alternatives considered

- **Carry defined net of financing.** Standard for leveraged desks but requires repo inputs;
  deferred to keep the MVP's inputs minimal.
- **Constant yield instead of constant z-spread.** Simpler, but conflates curve shape with
  credit/liquidity spread and makes roll-down invisible. Rejected.
- **Separating pull-to-par from roll-down.** A finer breakdown, but requires a further
  convention (constant-yield path). May be added as an additional column.
- **Stochastic rate simulation.** A different class of tool; out of scope.

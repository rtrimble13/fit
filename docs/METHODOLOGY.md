# Methodology

This document defines every number `fit` produces: the conventions, the formulas and the
approximations. It is the reference to check the tool against another system. Design
rationale lives in the [ADRs](adr/README.md); usage in the [walk-throughs](walkthroughs/README.md).

Notation: rates are decimals unless stated; `DF(t)` is a discount factor; `f` is the number
of coupons per year; prices are per 100 face.

1. [Dates and calendars](#1-dates-and-calendars)
2. [Day counts and schedules](#2-day-counts-and-schedules)
3. [Yield curves](#3-yield-curves)
4. [Bootstrapping](#4-bootstrapping)
5. [Bonds](#5-bonds)
6. [Horizon analysis](#6-horizon-analysis)
7. [Portfolios and pre-trade](#7-portfolios-and-pre-trade)
8. [Numerical methods and tolerances](#8-numerical-methods-and-tolerances)
9. [Validation](#9-validation)

## 1. Dates and calendars

Dates are proleptic Gregorian, 1900-01-01 to 2299-12-31. Adding months clamps to the last
day of the target month (31 Jan + 1M = 28/29 Feb).

**Business-day conventions:** `unadjusted`, `following`, `modified-following` (following,
unless that crosses a month end, then preceding) and `preceding`.

**Calendars** (`--calendar`):

| Name | Non-business days |
|------|-------------------|
| `none` | none |
| `weekends` (default) | Saturdays and Sundays |
| `us` | weekends plus US government bond market holidays |

The `us` calendar is rule-based: New Year's Day (Monday if it falls on a Sunday), Martin
Luther King Jr. Day, Washington's Birthday, Good Friday (a business day from 1996 when it
is the first Friday of the month), Memorial Day, Juneteenth (from 2022), Independence Day,
Labor Day, Columbus Day, Veterans Day (Monday if on a Sunday; not observed on the preceding
Friday), Thanksgiving and Christmas; fixed-date holidays other than those noted move to
Friday from a Saturday and to Monday from a Sunday. Three historical one-off closures are
included (2004-06-11, 2012-10-30, 2018-12-05). It agrees day for day with QuantLib's
`UnitedStates(GovernmentBond)` calendar from 1990 to 2060. Early closes are business days.
`--holidays FILE` adds further dates to any calendar.

## 2. Day counts and schedules

| Name | Year fraction from `d1` to `d2` |
|------|---------------------------------|
| `act/360` | actual days / 360 |
| `act/365` | actual days / 365 |
| `30/360` | 30/360 US (bond basis): `(360(Y2-Y1) + 30(M2-M1) + (D2-D1)) / 360` after: if `d1` is the last day of February, `D1 = 30` (and `D2 = 30` if `d2` is too); if `D2 = 31` and `D1 >= 30`, `D2 = 30`; if `D1 = 31`, `D1 = 30` |
| `30e/360` | as above with only: `D1 = min(D1, 30)`, `D2 = min(D2, 30)` |
| `act/act-isda` | days falling in a leap year / 366 + days falling in a non-leap year / 365 |
| `act/act` (ICMA) | `(d2 - d1) / (f x (ref_end - ref_start))`, relative to the regular coupon period `[ref_start, ref_end]` containing the dates |

**Coupon schedules** are generated backward from maturity in steps of `12/f` months, each
date computed directly from the maturity date. If the maturity is a month end, coupon dates
are month ends (disable with `--no-eom`). Generation stops at the dated date (`--issue`);
if that falls inside a period, the first period is a **short stub** whose reference period
is the full regular period ending on the first coupon date. Without a dated date the
schedule is regular.

Bond accrual dates are never business-day adjusted. Payment dates are adjusted with the
`following` convention on the chosen calendar. Swaps differ: their periods accrue between
adjusted dates (section 4).

## 3. Yield curves

A curve is an as-of date plus nodes `(t_i, z_i)`: `t` is the ACT/365F time from the as-of
date and `z` the continuously compounded zero rate, so

```
DF(t) = exp(-z(t) t)
```

**Interpolation** (`--interp`). The last column is each scheme's default extrapolation; see
below for `--extrapolation`.

| Scheme | Rule between nodes | Before first node | After last node |
|--------|--------------------|-------------------|-----------------|
| `flat-forward` (default) | `-ln DF` linear in `t`, anchored at `(0, 0)` | zero rate flat | last forward rate flat |
| `linear-zero` | `z` linear in `t` | zero rate flat | zero rate flat |
| `cubic-zero` | natural cubic spline through `(t_i, z_i)` | zero rate flat | zero rate flat |
| `monotone-cubic` | monotone cubic Hermite (Fritsch-Carlson, "PCHIP") through `(t_i, z_i t_i)` anchored at `(0, 0)` | follows the spline | last forward rate flat |
| `monotone-convex` | Hagan-West monotone convex through `(t_i, z_i t_i)` anchored at `(0, 0)` (below) | follows the scheme | instantaneous forward at the last node held flat |

`monotone-cubic` preserves the monotonicity of `-ln DF`, so interpolated forward rates are
non-negative wherever the node discount factors are non-increasing.

**Extrapolation** (`--extrapolation`; decision in
[ADR 0015](adr/0015-selectable-extrapolation.md)). Beyond the last pillar `t_n`, with zero rate
`z_n` there:

| Rule | Definition | Forward at `t_n` |
|------|------------|------------------|
| `flat-forward` | `-ln DF(t) = z_n t_n + f_n (t - t_n)`. On the `-ln DF` schemes `f_n` is the slope of the interpolant at `t_n`; on the zero-rate schemes `f_n = z_n + t_n z'(t_n)`, with `z'` the slope of the zero interpolant there | continuous |
| `flat-zero` | `DF(t) = exp(-z_n t)` | jumps from `f_n` to `z_n` |

Without the option, each scheme extrapolates as in the table above: `flat-zero` for
`linear-zero` and `cubic-zero`, `flat-forward` for the others. `fit curve build` reports the
rule in use. Before the first pillar the behaviour is fixed (see the table above) and does not
depend on the option. The bootstrap solves the same nodes under either rule, because no
instrument pays beyond its own pillar.

**Monotone convex** (P. S. Hagan and G. West, "Interpolation methods for curve construction",
*Applied Mathematical Finance* 13(2), 2006; "Methods for constructing a yield curve", *Wilmott
Magazine*, May 2008; decisions in [ADR 0014](adr/0014-monotone-convex-interpolation.md)).
The scheme constructs the instantaneous forward `f(t)` directly. With nodes
`t_0 = 0 < t_1 < ... < t_n`, `h_i = t_i - t_(i-1)` and discrete forwards
`fd_i = (z_i t_i - z_(i-1) t_(i-1)) / h_i`:

1. Node forwards. Interior nodes use `f_i = (h_i fd_(i+1) + h_(i+1) fd_i) / (h_i + h_(i+1))`,
   which is linear interpolation of the discrete forwards placed at their interval midpoints.
   The ends use `f_0 = fd_1 - (f_1 - fd_1) / 2` and `f_n = fd_n - (f_(n-1) - fd_n) / 2`,
   computed before step 2. A curve with one pillar has `f_0 = f_1 = fd_1`, which makes it a
   flat curve.
2. Positivity. Each node forward is clamped to `[0, 2 min(adjacent fd)]` if every adjacent
   discrete forward is non-negative. The end nodes have one adjacent forward, so their bounds
   are `[0, 2 fd_1]` and `[0, 2 fd_n]`. A node next to a negative discrete forward is not
   clamped, so on negative-rate curves the scheme is the unconstrained method.
3. Shape. On interval `i`, let `u = (t - t_(i-1)) / h_i`, `g0 = f_(i-1) - fd_i` and
   `g1 = f_i - fd_i`. Then `f(t) = fd_i + g(u)`, where `g` depends on the region:

| Region | Condition | `g(u)` |
|--------|-----------|--------|
| (i) | `g0`, `g1` of opposite sign, `abs(g0)/2 <= abs(g1) <= 2 abs(g0)` | `g0 (1 - 4u + 3u^2) + g1 (3u^2 - 2u)` |
| (ii) | opposite sign, `abs(g1) > 2 abs(g0)` | `g0` for `u <= eta`, then `g0 + (g1 - g0) ((u - eta)/(1 - eta))^2`; `eta = (g1 + 2 g0)/(g1 - g0)` |
| (iii) | opposite sign, `abs(g1) < abs(g0)/2` | `g1 + (g0 - g1) ((eta - u)/eta)^2` for `u < eta`, then `g1`; `eta = 3 g1/(g1 - g0)` |
| (iv) | same sign | `A + (g0 - A) ((eta - u)/eta)^2` for `u < eta`, then `A + (g1 - A) ((u - eta)/(1 - eta))^2`; `eta = g1/(g0 + g1)`, `A = -g0 g1/(g0 + g1)` |
| | `g0 = 0` or `g1 = 0` | `0`, the limit of the regions above |

Every `g` integrates to zero over its interval, so `f` averages to `fd_i` and the curve
reprices its nodes: `-ln DF(t) = z_(i-1) t_(i-1) + h_i (fd_i u + integral of g from 0 to u)`.
The forward is continuous, including at the pillars. On an interval whose discrete forward lies
between its neighbours', the forward is monotone. Where the discrete forwards are non-negative,
it is non-negative. Changing a pillar moves the forward on at most two intervals on each side.

In one degenerate case the forward does jump. When two adjacent discrete forwards are equal and
the next differs, the limit of the construction is a step in the forward at a pillar. Discount
factors stay continuous in the inputs, so the bootstrap and risk are unaffected.

**Derived rates:**

| Quantity | Definition |
|----------|------------|
| zero rate, compounded `f` times | `f (exp(z/f) - 1)` |
| forward rate, continuous, `d1` to `d2` | `ln(DF(d1)/DF(d2)) / (t2 - t1)` |
| forward rate, simple | `(DF(d1)/DF(d2) - 1) / tau(d1, d2)` in the stated day count |
| instantaneous forward at `t` | continuous forward over the one-day interval centred on `t` |
| par rate to `T` (`par_sa_pct`) | `(1 - DF(T)) / sum_i tau_i DF(T_i)` over a semi-annual ACT/ACT schedule from the as-of date, payments adjusted following: the same instrument as a `par` quote with no spot lag |

**Curve transformations**, all by composition on discount factors:

| Curve | Definition | Used for |
|-------|------------|----------|
| shifted | `DF'(t) = DF(t) exp(-s(t) t)` | scenarios, risk |
| forward-implied at `h` | `DF_h(t) = DF(t_h + t) / DF(t_h)`, as-of date `h` | "forwards realised" |
| re-based at `h` | `DF_h(t) = DF(t)`, as-of date `h` | "unchanged curve" |

The shift `s(t)` is a spread on continuously compounded zero rates. A **parallel** shift is
constant. A **twist** `T1:b1/T2:b2/...` is linear between the given tenors and flat outside
them. A **key-rate** bump at tenor `T_k` is the triangle that equals the bump at `T_k`, falls
linearly to zero at `T_(k-1)` and `T_(k+1)`, and is flat before the first and after the last
key tenor; the bumps over all key tenors sum to a parallel shift. Tenors convert to years as
months/12.

## 4. Bootstrapping

Each quote defines an instrument and a model-implied rate on a trial curve. `S` is the
start date: the as-of date advanced by the spot lag (`--spot-lag`, or the row's
`settle_days`) in business days. `E` is `S` plus the tenor (or the explicit maturity date).

| Type | Model-implied rate | Pillar date |
|------|--------------------|-------------|
| `deposit` | `(DF(S)/DF(E') - 1) / tau(S, E')`, `E'` = `E` adjusted modified-following | `E'` |
| `swap` | `(DF(S) - DF(P_n)) / sum_i tau_i DF(P_i)` over the fixed schedule. Each period accrues from the previous adjusted date (`S` for the first) to its own end adjusted modified-following, `P_i`, and is paid on `P_i`, as in ISDA swaps | last payment |
| `irs` | the same formula on one curve. On a projection curve, see below | last payment |
| `basis` | the spread on the reference leg that makes a floating vs floating swap fair; see "Basis swaps" below | `E'` |
| `fra` `AxB` | `(DF(F_s)/DF(F_e) - 1) / tau(F_s, F_e)`: the simple forward from `F_s` = `S` + A months to `F_e` = `F_s` + (B - A) months, each adjusted modified-following (QuantLib's `FraRateHelper` dates) | `F_e` |
| `future` | futures rate `(100 - price) / 100` = forward + convexity adjustment, the forward being the simple rate from the contract month's IMM date (third Wednesday) to the IMM date three months later, unadjusted | the later IMM date |
| `par` | same formula over the coupon schedule, payments adjusted following; equivalent to a bond with that coupon pricing at 100 on `S` | last payment |
| `zero` | `z(E)`; no spot lag | `E` |

Defaults: `deposit`, `fra` and `future` ACT/360; `swap` annual, ACT/360; `irs` annual ACT/360
fixed against quarterly floating periods (`float_frequency`, a 3M term rate); `basis`
quarterly reference leg (`frequency`) against a quarterly leg on this curve's index
(`float_frequency`), the spread accruing ACT/360; `par` semi-annual, ACT/ACT ICMA.

**Futures.** A `future` is a quarterly IMM contract, named by a code such as `Z6` or `Z26` (H
March, M June, U September, Z December). A one-digit year is the first year from the as-of
year on that ends in it. It is quoted by `price`, and its `convexity_bp` is the futures rate
minus the forward rate, in bp, supplied by the user; there is no convexity model. For a
compounded-overnight future such as 3M SOFR, compounding over the period telescopes on the
curve to the same simple forward, so one formula serves term-rate and overnight futures. A
contract whose period has started needs past fixings and is rejected.
Schedules are generated as in section 2 with `S` as the dated date. The `swap` formula is the
single-curve result: the floating leg is worth `DF(S) - DF(P_n)`.

**Payment lags.** With a payment lag of `L` business days (`--payment-lag`, the curve set's
`payment_lag`, or the row's `payment_lag`), every leg of a `swap`, `irs` or `basis` quote still
accrues from `P_(i-1)` to `P_i`, but pays on `P_i + L` business days, `Q_i`. Each coupon is
discounted at `DF(Q_i)`, and the pillar is the last payment date `Q_n`, as in QuantLib's
`OISRateHelper` and basis-swap helpers. On one curve the floating coupons then no longer
telescope, and the `swap` rate becomes

`sum_i DF(Q_i) (DF(P_(i-1))/DF(P_i) - 1) / sum_i tau_i DF(Q_i)`.

USD SOFR swaps, for example, pay two business days after each period ends. Deposits, FRAs,
futures, par bonds and zero rates have no swap legs and ignore the lag. The default is no lag.

**Algorithm.** Quotes are sorted by pillar date; two quotes with the same pillar are an
error. The node zero rates start at the quoted rates. Then, repeatedly, for each pillar in
order, the node's zero rate is solved by Brent's method so that its own instrument reprices
with all other nodes held fixed. (On the first sweep, nodes not yet solved are kept level
with the node being solved, which makes that sweep a classic sequential bootstrap.) Sweeps
stop when the largest repricing error over all quotes is at most `1e-12` in rate terms
(`1e-8` bp). Local schemes converge in one sweep; `cubic-zero` typically needs four to six,
and `monotone-convex` three or four.
If 100 sweeps do not converge the command fails; a curve that does not reprice is never
returned.

**Projection curves** ([ADR 0016](adr/0016-multi-curve-construction.md)). A projection curve
`P` is bootstrapped on an already built discount curve `D`, for example a 3M term-rate curve on
the SOFR OIS curve. The algorithm is the same, with `D` held fixed. Floating legs are projected
on `P` and every leg is discounted on `D`:

| Type | Model-implied rate on `P` with discount curve `D` |
|------|---------------------------------------------------|
| `deposit` | `(P(S)/P(E') - 1) / tau(S, E')`: a term-rate fixing |
| `swap` | `sum_j D(P_j) (P(P_(j-1))/P(P_j) - 1) / sum_i tau_i D(P_i)` over the fixed schedule (overnight compounding telescopes within each period on `P`) |
| `irs` | the same, with the floating sum over the floating schedule (`float_frequency` periods a year, adjusted modified-following) |
| `fra`, `future` | as on one curve: the simple forward of `P` over the period (plus the convexity adjustment for a future) |
| `zero` | `z_P(E)` |
| `par` | not allowed |

Floating coupons are par coupons: each pays the forward of `P` over its own accrual period, so
the floating day count cancels. When `P` and `D` are the same curve the floating legs telescope
to `D(S) - D(P_n)` and the single-curve formulas above result.

**Basis swaps** ([ADR 0019](adr/0019-basis-swaps-and-reference-curves.md)). A `basis` quote is
a floating vs floating swap from `S` to `E` with the spread `s` (`spread_bp`) paid on one leg.
That leg, the reference leg, pays the index of an already built reference curve `R`,
`frequency` times a year, plus `s`. The other leg pays the index of the curve being built, `P`,
`float_frequency` times a year. Both legs are discounted on `D`. Each leg's periods run between
dates adjusted modified-following, from `S`, and each is paid at its adjusted end, as for
`irs`. The model-implied spread is

`s = [ sum_j D(Q_j) (P(Q_(j-1))/P(Q_j) - 1) - sum_i D(P_i) (R(P_(i-1))/R(P_i) - 1) ] / sum_i tau_i D(P_i)`

over the reference leg's dates `P_i` and this curve's leg's dates `Q_j`, with `tau` on the
quote's day count (default ACT/360). A positive spread means this curve's index fixes above the
reference's, so a 6M curve quoted at 3M + 5bp sits above the 3M curve. On a projection curve
the reference defaults to the discount curve: for `--discount-curve sofr --projection-curve
term`, a basis quote prices compounded SOFR + spread against the term rate. Compounding
overnight over a period telescopes on `R` to the same par coupon, so one formula serves
overnight and term-rate reference legs. A curve set can name another reference curve in its
`reference` column, such as the 3M curve for a 6M curve. A curve with neither a discount curve
nor a reference curve cannot take basis quotes. The reference curve, like the discount curve,
is used as built: past its last pillar it extrapolates under its own rule.

These are the conventions of QuantLib's `OvernightIborBasisSwapRateHelper` and
`IborIborBasisSwapRateHelper` with the spread on the base leg and par coupons. The spread is
always on the reference leg; a market that quotes the spread on the other leg needs its quotes
converted first.

A curve set ([ADR 0017](adr/0017-curve-sets.md)) builds each of its curves after the curve that
discounts it and the reference curve of its basis quotes. A bond priced against any curve of a
market is discounted on that curve's discount curve.

## 5. Bonds

A bond pays `100 c tau_i` at the end of each period, where `tau_i` is the year fraction of
the period in the bond's day count (so `c/f` for a regular period under `act/act` or
`30/360`), plus the redemption value at maturity. Settlement defaults to the trade date plus
`--settle-days` business days (default 1).

**Accrued interest** at settlement `s` in the period starting `a`:

```
AI = 100 c tau(a, s)
```

A cashflow belongs to the buyer when its nominal (unadjusted) coupon date is after the
settlement date. Ownership follows nominal dates everywhere; curve discounting uses the
adjusted payment dates.

### 5.1 Yield (street convention)

```
P_dirty(y) = sum_i CF_i (1 + y/f)^(-f T_i)
```

`T_i` is measured in the bond's day count along the *unadjusted* coupon dates:
`T_1 = tau(a, e_1) - tau(a, s)` for the current period `[a, e_1]`, and
`T_i = T_(i-1) + tau_i` after that. For regular periods `f T_i` is the familiar
`w + (i - 1)`, with `w` the fraction of the current period remaining. The same formula is
used in the final coupon period (no switch to simple interest). Yield from price inverts
this by root-finding.

| Measure | Definition |
|---------|------------|
| Macaulay duration | `sum_i T_i PV_i / P` |
| modified duration | Macaulay / `(1 + y/f)` |
| convexity | `sum_i T_i (T_i + 1/f) PV_i / (P (1 + y/f)^2)` |
| DV01 (`dv01`, `yield_dv01`) | modified duration x `P` x 0.0001 |

where `PV_i = CF_i (1 + y/f)^(-f T_i)` and `P` is the dirty price.

### 5.2 Curve pricing and z-spread

```
P_dirty(z) = sum_i CF_i  DF(p_i)/DF(s)  exp(-z (t_i - t_s))
```

over the buyer's cashflows, each discounted from its payment date `p_i`. Discounting runs from the
settlement date. The **z-spread** `z` is a constant spread on continuously compounded
ACT/365F zero rates; given a price it is solved by root-finding. Note that it is *not*
quoted with the bond's compounding: a z-spread of `z` continuous corresponds to roughly
`z (1 + y/f)` in the bond's own compounding.

### 5.3 Curve risk

With `P(shift)` the dirty price on the shifted curve at constant z-spread and `b` the bump
(default 1bp, `--bump-bp`):

| Measure | Definition |
|---------|------------|
| `curve_dv01` | `(P(-b) - P(+b)) / 2`, scaled to 1bp |
| effective duration | `(P(-b) - P(+b)) / (2 b P)` |
| effective convexity | `(P(-b) + P(+b) - 2P) / (b^2 P)` |
| key-rate DV01 at `T_k` | `(P(-b_k) - P(+b_k)) / 2`, with `b_k` the key-rate bump of section 3 |

Bumps are applied to **zero rates**, not to the market quotes; the curve is not re-bootstrapped.
Key-rate DV01s therefore sum to `curve_dv01`. Because the bumped rate is continuously
compounded, `curve_dv01` exceeds the yield DV01 by approximately the factor `1 + y/f`.

## 6. Horizon analysis

Inputs: settlement `s`, horizon date `h` (`s < h <` maturity), today's curve, the bond's
mark, a base scenario and a shock. Everything is per 100 face.

1. **Today.** Dirty price `P_0`, accrued `AI_0` and z-spread `z` from the mark (the
   z-spread is solved when the mark is a price or yield).
2. **Horizon curve.** The horizon curve's as-of date is `h` minus the lag between today's
   as-of and settlement dates. Under `unchanged` it is today's curve re-based to that date;
   under `forward` it is the forward-implied curve. The shock is then added as a zero-rate
   shift.
3. **Horizon price.** `P_h` is the curve price of the cashflows falling due after `h`, at the same
   `z`. `P_h_base` is the same without the shock.
4. **Coupons.** Cashflows with a nominal date after `s` and on or before `h` total `C`. Each is reinvested
   from its payment date `p` to `h`: by default at today's forwards, growth
   `DF(p)/DF(h)`; with `--reinvest r`, growth `(1 + r/f)^(f (h - p)/365)`. `R` is the
   total interest earned.

| Output | Definition |
|--------|------------|
| `coupon_income` | `C + AI_h - AI_0` |
| `roll` | `(P_h_base - AI_h) - (P_0 - AI_0)` |
| `curve_shift` | `P_h - P_h_base` |
| `reinvestment` | `R` |
| `total_pnl` | `P_h + C + R - P_0`, the sum of the four lines above |
| `return_pct` | `total_pnl / P_0` |
| `annualized_pct` | `(1 + return)^(365 / (h - s)) - 1` |

Properties worth knowing:

- Under `forward` with default reinvestment and no shock, a bond priced flat to the curve
  (`z = 0`) returns exactly `DF(s)/DF(h) - 1`, whatever its coupon or maturity: every bond
  earns the curve's rate to the horizon. A zero-coupon bond at spread `z` earns
  `exp(z (h - s)/365) DF(s)/DF(h) - 1`. Both are tested exactly.
- `roll` under `unchanged` is the classic roll-down (plus pull to par); under `forward` it is
  the price change implied by the forwards.

Not modelled: financing (repo), changes in spread, default, taxes and transaction costs.

## 7. Portfolios and pre-trade

Each position is valued at one settlement date (default: the as-of date). A position with a
market `price` is marked to it and its z-spread implied; otherwise it is priced on the curve
at its `zspread` (default 0).

| Output | Definition |
|--------|------------|
| `market_value` | `face x P_dirty / 100` |
| `accrued` | `face x AI / 100` |
| position `dv01`, key-rate DV01 | `face / 100` x the per-100 curve measures of section 5.3 |
| position `duration` | effective duration |
| total `dv01`, key-rate DV01, `market_value` | sums over positions |
| total `duration` | `dv01 x 10000 / market_value` |
| total `convexity`, `yield_pct` | market-value weighted averages |

Ratios are reported as `n/a` when net market value is zero.

**Scenario P&L** reprices every position on the shifted curve at its own constant z-spread:
exact repricing, not a duration approximation. **Cashflow ladders** aggregate
`face / 100 x` coupon and principal by payment date; `curve_pv` discounts on the curve
without spread.

**Pre-trade** values the portfolio, the trades and their union identically and reports the
three side by side. `settlement_amount = face x P_dirty / 100` at the valuation settlement
date. The DV01 limit check compares `|total dv01|` after trading with the limit.

## 8. Numerical methods and tolerances

| Item | Method | Tolerance |
|------|--------|-----------|
| Arithmetic | IEEE-754 double precision; no fast-math | |
| Root-finding | Brent's method on an automatically widened bracket | `1e-13` on the root (`1e-15` in the bootstrap) |
| Yield search range | -50% to 1000% | |
| Z-spread search range | +/- 1000% | |
| Bootstrap zero-rate range | -25% to 200% | |
| Bootstrap convergence | max repricing error | `1e-12` (rate, decimal) |
| Risk | central differences, 1bp default bump | |

Text tables round for display; CSV and JSON carry 12 significant digits.

## 9. Validation

Expected values in the test suite come from sources independent of this code base:

| Area | Reference | Agreement |
|------|-----------|-----------|
| Price, yield, Macaulay and modified duration | published Microsoft Excel examples for `PRICE`, `YIELD`, `DURATION`, `MDURATION` | to the published digits |
| Accrued, clean price, durations, convexity across day counts, frequencies, stubs and month-end schedules | QuantLib `BondFunctions` | `1e-9` relative |
| Bootstrapped discount factors (`flat-forward`, `linear-zero`) at and between pillars | QuantLib `PiecewiseLogLinearDiscount`, `PiecewiseLinearZero` | `1e-9` |
| Bootstrapped SOFR OIS discount factors (`swap` quotes, spot lag, US calendar) at and between pillars | QuantLib 1.44 `OISRateHelper` with default (modified-following, annual, ACT/360) conventions | `1e-10` |
| Dual-curve projection curve (`deposit` and `irs` on the SOFR OIS curve; `flat-forward` and `linear-zero`) at and between pillars | QuantLib 1.44 `SwapRateHelper` with a 3M `IborIndex`, an exogenous discounting curve and par coupons; 3M forwards agree to `1e-11`% | `1e-10` |
| SOFR curve from a 3M SOFR futures strip with convexity adjustments, and a 3M projection curve with FRAs (`flat-forward` and `linear-zero`) | QuantLib 1.44 `SofrFutureRateHelper`, `FraRateHelper` | `1e-10` (agreement 1.7e-12) |
| IMM dates, FRA and futures dates, futures rate and convexity sign | hand calculation; QuantLib helpers' own start and end dates | exact |
| 3M Term SOFR curve from SOFR basis swaps on the SOFR curve, and 6M Term SOFR from 3M / 6M basis swaps against the 3M curve (`flat-forward` and `linear-zero`) at and between pillars | QuantLib 1.44 `OvernightIborBasisSwapRateHelper`, `IborIborBasisSwapRateHelper` (spread on the base leg, par coupons) | `1e-10` (agreement 1.9e-12) |
| Basis spread on flat curves, with different leg frequencies; a basis swap as the difference of two swaps | hand calculation; identity | `1e-15`, `1e-14` |
| Payment lags: the SOFR curve and both basis curves with 2-day lags, at and between pillars, and their pillar dates | QuantLib 1.44 `OISRateHelper`, `OvernightIborBasisSwapRateHelper`, `IborIborBasisSwapRateHelper` with `paymentLag` 2 | `1e-10` (agreement 4.7e-13) |
| Lagged `irs` quotes on a projection curve | each quote repriced by QuantLib `FixedRateLeg` and `IborLeg` with `paymentLag` 2 on fit's curves | `1e-12`; pillars exact |
| A lagged swap on a flat curve | hand calculation | `1e-15` |
| Floating legs on a projection curve | hand calculation on flat curves | `1e-15` |
| Curve price with z-spread | QuantLib `ZeroSpreadedTermStructure` | `1e-8` |
| Natural cubic and monotone cubic interpolation | SciPy `CubicSpline(bc_type="natural")`, `PchipInterpolator` | `1e-11` |
| Monotone convex interpolation: forwards in every region, with uneven spacing and negative and mixed-sign rates | QuantLib 1.44 `ConvexMonotoneInterpolation` with quadraticity 0, monotonicity 1, `forcePositive` false (the published method); hand calculations for each region and for the positivity bounds | `1e-15` |
| Bootstrapped `monotone-convex` discount factors at and between pillars | QuantLib 1.44 `PiecewiseConvexMonotoneForward` with `ConvexMonotone(0, 1, false)` | `1e-10` |
| `flat-forward` extrapolation to 50 years: `flat-forward` and `linear-zero` curves | QuantLib 1.44 `PiecewiseLogLinearDiscount`, `PiecewiseLinearZero` with extrapolation enabled | `1e-10` |
| `flat-zero` extrapolation and `flat-forward` on `cubic-zero` | hand calculation | `1e-14` |
| US government bond calendar | QuantLib, every day 1990-2060 | exact |
| Day counts | ISDA ACT/ACT examples | exact |

and from identities that must hold exactly: every quote reprices under every interpolation
scheme; price and yield round-trip; z-spread round-trips; key-rate DV01s sum to the parallel
DV01; horizon components sum to total P&L; the forward-scenario return equals the curve rate
plus spread.

`tools/quantlib_crosscheck.py` re-runs the QuantLib comparisons against a built binary.

`cubic-zero` is validated by repricing and by the spline test rather than against QuantLib's
cubic zero curve, which places an additional node at `t = 0` and so differs between pillars
by construction.

`monotone-convex` is compared with QuantLib only inside the pillar range. Beyond the last
pillar, QuantLib's piecewise forward curve holds the last discrete forward, while `fit` holds
the last instantaneous forward so that forwards stay continuous. QuantLib's default
`ConvexMonotone(0.3, 0.7, true)` blends in other shapes and applies positivity differently, so
it is not the published method.

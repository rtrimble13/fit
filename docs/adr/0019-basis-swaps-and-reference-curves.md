# 0019. Basis swaps and reference curves

- **Status:** Proposed
- **Date:** 2026-11-25

## Context

[ADR 0016](0016-multi-curve-construction.md) builds a projection curve on a discount curve from
deposits, FRAs, futures and fixed vs floating swaps. Markets also quote floating vs floating
swaps, called basis swaps. A desk that marks a 6M Term SOFR curve has few liquid 6M fixed-rate
swaps but liquid 3M vs 6M basis swaps, and builds the 6M curve from the 3M curve plus the basis.
Likewise a term-rate curve can be built from SOFR vs term-rate basis swaps on the SOFR curve.
The roadmap lists basis swaps under "Additional instruments for construction". The known
limitations note that no instrument ties curves together.

A basis swap's price depends on three curves: the discount curve, the curve whose index the
quoted leg pays, and the curve being built. ADR 0016 and [ADR 0017](0017-curve-sets.md) give a
curve one dependency only, its discount curve.

QuantLib 1.44 has `OvernightIborBasisSwapRateHelper` and `IborIborBasisSwapRateHelper`. In
both, one leg pays a base index plus the quoted spread and the other pays a second index. Each
leg pays at its own index's frequency, coupons are par coupons by default, and the helper can
bootstrap the curve of either leg.

## Decision

We will add a `basis` quote type and a reference curve for it.

- **Instrument.** A `basis` quote is a swap from spot to the tenor. The reference leg pays the
  reference curve's index plus the quoted spread, `frequency` times a year (default 4). The
  other leg pays the index of the curve being built, `float_frequency` times a year (default
  4). The spread accrues on the quote's day count (default ACT/360). Both legs are discounted
  on the curve's discount curve.
- **Schedules and coupons.** Both legs use the `irs` schedules: periods between business-day
  adjusted dates, each paid at its adjusted end. Coupons are par coupons on the leg's curve
  ([METHODOLOGY.md](../METHODOLOGY.md#4-bootstrapping)). Overnight compounding telescopes to the
  same coupon, so an overnight reference leg needs no separate formula.
- **Quote.** The quote is the spread, in bp, in a new `spread_bp` column. A positive spread
  means the built curve's index fixes above the reference's. `rate` is not allowed on a `basis`
  row, and `spread_bp` is not allowed on any other.
- **Spread leg.** The spread is always on the reference leg, as with QuantLib's base leg. Desks
  usually quote the spread on the shorter tenor or the overnight leg, which is the reference
  when a longer tenor is built from it.
- **Reference curve.** By default the reference curve is the curve's discount curve: with
  `--discount-curve` and `--projection-curve`, basis quotes price the discount curve's index
  against the projection curve's. A curve set may name another curve in a new optional
  `reference` column. A curve with basis quotes and neither a discount curve nor a reference
  curve is an error.
- **Build order.** A curve set builds each curve after its discount curve and its reference
  curve, otherwise in file order. A cycle through either is an error naming the curves. So is
  an unknown reference curve, a curve naming itself, or a reference on a curve with no basis
  quotes.
- **Library.** `BootstrapOptions` gains a `reference` curve. `implied_quote_rate` takes an
  optional reference curve. The leg formula is exposed as `basis_spread`.
- **Validation.** The bootstrap is compared with QuantLib's two helpers: a 3M Term SOFR curve
  from SOFR basis swaps, and a 6M curve from 3M vs 6M basis swaps against the 3M curve. Both
  use flat-forward and linear-zero interpolation.

Output tables are unchanged: a basis quote appears in the nodes table with its spread in
`quote_pct` and its repricing error in `error_bp`.

## Consequences

- Curves for further tenors (1M, 6M, 12M) and term-rate curves can be built from the
  instruments desks actually quote, on the same sequential bootstrap.
- Curves are still built one after another, not jointly. A reference curve must be fully
  determined by its own quotes first. Structures where two curves each need the other's quotes,
  such as an OIS curve built from basis swaps against a curve discounted on it, are rejected as
  cycles.
- The spread-leg convention is fixed. A market that quotes the spread on the leg of the curve
  being built must convert the quote. The conversion is not an exact sign change when the legs
  have different frequencies, because the two legs' annuities differ.
- The reference curve is used as built, including its extrapolation, as the discount curve
  already is. A basis quote beyond the reference curve's last pillar relies on that
  extrapolation.
- Curve-set files gain an optional column, and the curve-set cycle error is reworded to cover
  both dependencies. Existing files and outputs are unchanged.
- The `curves` summary table does not show the reference curve; it is in the curve-set file.

## Alternatives considered

- **Name the reference curve in each quotes row.** This allows several references in one
  curve, but ties a quotes file to the names of one particular curve set, and it does not work
  with the pair, whose curves have fixed names. One reference per curve covers the desk case of
  one tenor built from another. Rejected for now.
- **A joint (simultaneous) bootstrap of all curves.** This would handle mutual dependencies, but
  needs a multi-dimensional solver and changes every multi-curve build. The sequential build
  covers the instruments desks use to build one curve from another. Not chosen; the roadmap
  keeps the limitation.
- **A column choosing the spread leg** (QuantLib's `basisOnIborLeg`). It would avoid converting
  quotes, but no example or user needs it yet ([ROADMAP.md](../ROADMAP.md), small surface).
  Deferred.
- **Quote the spread in `rate`, in percent.** This avoids a column, but basis is quoted in bp
  and `rate` would mean a spread on one type only. Rejected, as `price` was for futures.
- **Add a `reference_curve` column to the `curves` table.** This is more transparent, but it
  changes the output of every curve-set build. Deferred until a user asks for it.

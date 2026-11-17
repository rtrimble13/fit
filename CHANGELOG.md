# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html). While the major
version is 0, minor releases may contain breaking changes; these are called out explicitly.

Changes that alter a computed number for existing inputs are listed under **Numerical changes**
with the reason and the size of the effect.

## [Unreleased]

### Added

- `monotone-convex` interpolation (`--interp monotone-convex`). It implements the monotone
  convex method of Hagan and West on `-ln DF`. Instantaneous forwards are continuous at the
  pillars and positive where the discrete forwards are. Where the discrete forwards are
  monotone, the forwards do not overshoot them. Before the first pillar the scheme follows its
  own construction. Beyond the last pillar it holds the last instantaneous forward. Validated
  against QuantLib 1.44 `ConvexMonotone(0, 1, false)`, both the interpolator and the bootstrap,
  and against hand calculations. See
  [ADR 0014](docs/adr/0014-monotone-convex-interpolation.md). The default interpolation is
  still `flat-forward`, and no existing output changes.
- `--extrapolation flat-forward|flat-zero` on every command that builds a curve, choosing how
  the curve continues beyond its last pillar. `flat-forward` holds the instantaneous forward at
  the last pillar, as QuantLib's interpolated curves do. `flat-zero` holds the last zero rate.
  The default keeps each scheme's existing behaviour: `flat-zero` for `linear-zero` and
  `cubic-zero`, `flat-forward` otherwise. Validated against QuantLib 1.44 out to 50 years. See
  [ADR 0015](docs/adr/0015-selectable-extrapolation.md).
- `fit curve build` reports the extrapolation in its summary (`extrapolation` row/field). This
  is an additive output change, and no existing value changes.

### Fixed

- `swap` quotes accrue each fixed period between business-day adjusted dates, as ISDA swaps
  do. Previously the periods ran between unadjusted dates, while payments were adjusted.
  Bootstrapped SOFR discount factors now agree with QuantLib's `OISRateHelper` to 5e-13.
  Before the fix they differed by up to 1.5e-4.

### Numerical changes

- Curves built from `swap` quotes change wherever a fixed-leg accrual date falls on a
  non-business day. On `examples/data/usd_sofr.csv` (T+2, US calendar) the 2Y pillar zero rate
  moves +0.50bp and the 7Y +0.28bp, and every other pillar by less than 0.04bp. A 10Y 4.25% bond
  priced off that curve moves -0.0015 per 100. Curves without `swap` quotes, such as the example
  Treasury curve, are unchanged. `par` quotes keep the bond convention of unadjusted accrual.

## [0.1.0] - 2026-11-17

Initial MVP release.

### Added

#### Core library (`fit_core`)

- Date arithmetic, tenors, business-day conventions and coupon schedule generation.
- Calendars: weekends-only, rule-based US government bond calendar, and custom holiday files.
- Day-count conventions: ACT/360, ACT/365F, 30/360 US, 30E/360, ACT/ACT ISDA, ACT/ACT ICMA.
- Yield curves stored as continuously compounded zero rates on an ACT/365F time axis, with
  `flat-forward` (default), `linear-zero`, `cubic-zero` and `monotone-cubic` interpolation.
- Iterative single-curve bootstrap from `deposit`, `swap`, `par` and `zero` quotes using Brent
  root-finding, with per-instrument repricing residuals.
- Shifted (parallel and key-rate) and forward-implied curves by composition.
- Fixed-rate bullet and zero-coupon bonds: cash flows, accrued interest, clean and dirty price,
  street-convention yield to maturity, Macaulay and modified duration, convexity and yield DV01.
- Curve-based bond analytics: model price, z-spread, effective duration and convexity, curve
  DV01 and key-rate DV01s by 1bp central-difference bump-and-reprice.
- Horizon analysis under `unchanged` and `forward` curve scenarios with parallel shifts, twists
  and constant z-spread; P&L decomposition into coupon income/accrual, roll-down, curve shift and
  reinvestment; total and annualised return.
- Portfolio valuation from positions, marking to market price where supplied and to model
  otherwise; aggregated market value, DV01, duration, key-rate ladder, cash-flow ladder and
  scenario P&L.
- Pre-trade comparison of portfolio risk before and after proposed trades, with settlement
  amounts and an optional DV01 limit check.
- `fit::Error` exception type carrying contextual error messages.

#### Command-line interface (`fit`)

- `fit curve build | query | forwards`.
- `fit bond price | yield | cashflows | risk | forecast`.
- `fit portfolio value | risk | scenario | cashflows`.
- `fit pretrade`.
- Global options `--format table|csv|json` and `--precision`.
- Desk units at the boundary: percent for rates, basis points for spreads and shifts, prices per
  100 face, ISO 8601 dates, tenors such as `3M` and `10Y`.
- Results on standard output, diagnostics on standard error; exit codes `0` (success), `1`
  (data or runtime error) and `2` (usage error).

#### Project

- CMake build with `dev` (Debug, ASan and UBSan) and `release` presets; GCC 13+ and Clang 17+.
- doctest unit tests with independently sourced golden values, and CLI integration tests.
- Continuous integration on GCC and Clang with a formatting check.
- README, methodology document, walk-throughs with example data, architecture decision records,
  development roadmap, contributor guidelines and pull-request standards.

[Unreleased]: https://github.com/rtrimble13/fit/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/rtrimble13/fit/releases/tag/v0.1.0

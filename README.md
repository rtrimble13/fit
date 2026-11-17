# fit — Fixed Income Toolkit

`fit` is a command-line tool for fixed income analysis: yield curve construction and
interpolation, bond valuation and risk, return forecasting, portfolio valuation and
pre-trade what-if analysis.

It is built for desk use. Conventions are explicit, numbers are validated against
independent references, inputs are plain CSV, output is a readable table or exact CSV/JSON,
and a run either succeeds completely or fails with a clear message and a non-zero exit code.

```console
$ fit bond forecast --maturity 2036-11-15 --coupon 4.25 --price 99.9375 \
      --curve examples/data/usd_treasury.csv --asof 2026-11-17 \
      --horizon 1Y --shifts=-50,0,50
...
scenarios
=========
shift  clean_end  yield_end_pct  coupon_income    roll  curve_shift  reinvestment  total_pnl  return_pct  annualized_pct
-----  ---------  -------------  -------------  ------  -----------  ------------  ---------  ----------  --------------
-50bp   104.1400         3.7044         4.2498  0.3266       3.8759        0.0420     8.4944       8.497           8.497
0bp     100.2641         4.2144         4.2498  0.3266       0.0000        0.0420     4.6185       4.620           4.620
50bp     96.5483         4.7256         4.2498  0.3266      -3.7158        0.0420     0.9027       0.903           0.903
```

**Status:** v0.1.0, an MVP. The scope is deliberately narrow (single-currency, fixed-rate
bullet and zero-coupon bonds, single-curve bootstrapping) and done carefully. See
[Limitations](#limitations) and the [roadmap](docs/ROADMAP.md).

## Contents

- [What it does](#what-it-does)
- [Build and install](#build-and-install)
- [Five-minute tour](#five-minute-tour)
- [Command reference](#command-reference)
- [Input files](#input-files)
- [Units and conventions](#units-and-conventions)
- [Output formats and exit codes](#output-formats-and-exit-codes)
- [Accuracy and validation](#accuracy-and-validation)
- [Limitations](#limitations)
- [Documentation](#documentation)
- [Project layout](#project-layout)
- [Contributing](#contributing)
- [License](#license)

## What it does

| Area | Capabilities |
|------|--------------|
| **Yield curves** | Bootstrap a zero curve from deposits, swaps, par bond yields and zero rates. Five interpolation schemes (`flat-forward`, `linear-zero`, `cubic-zero`, `monotone-cubic`, and Hagan-West `monotone-convex` for continuous, positive forwards). Flat-forward or flat-zero extrapolation beyond the last pillar. Exact repricing of every input, reported per instrument. Zero, par, forward and instantaneous forward rates at any tenor or date; forward strips; forward-implied future curves. |
| **Bond valuation** | Price from yield, yield from price, price off a curve with a z-spread, implied z-spread from a price. Accrued interest, clean and dirty prices, cashflow schedules with discount factors. Six day-count conventions, short first coupons, month-end rolls, business-day adjusted payments. |
| **Risk** | Macaulay and modified duration, convexity, DV01. Effective duration and convexity and key-rate DV01s by bump-and-reprice on a user-defined tenor ladder. |
| **Forecasting** | Horizon analysis: projected price and total return to any horizon under an unchanged curve or realised forwards, with parallel shocks and curve twists. Return attribution into coupon income, roll-down, curve move and reinvestment. |
| **Portfolio valuation** | Mark-to-market and mark-to-model in one book, long and short. Aggregated market value, DV01, duration, convexity and yield; key-rate ladder by position; scenario P&L by full repricing; cashflow ladders. |
| **Pre-trade analysis** | Ticket economics (yield, spread to curve, settlement amount, DV01). Before/after comparison of portfolio value and risk for a package of trades. DV01 limit check. Stress tests of the post-trade book. |

## Build and install

Requirements: a C++20 compiler (GCC 13+ or Clang 17+), CMake 3.24+ and Ninja, on Linux.
There are no third-party runtime dependencies; the only vendored code is the
[doctest](https://github.com/doctest/doctest) header used by the tests.

```console
$ git clone https://github.com/rtrimble13/fit.git && cd fit
$ cmake --preset release
$ cmake --build --preset release
$ ctest --preset release
$ build/release/fit --version
fit 0.1.0
```

Install to a prefix (optional):

```console
$ cmake --install build/release --prefix ~/.local
```

The examples below assume `fit` is on your `PATH`:

```console
$ export PATH="$PWD/build/release:$PATH"
```

For development use the `dev` preset, a debug build with AddressSanitizer and
UndefinedBehaviorSanitizer enabled; see [CONTRIBUTING.md](CONTRIBUTING.md).

## Five-minute tour

All commands run from the repository root against the illustrative data in
[`examples/data`](examples/data). Full explanations are in the
[walk-throughs](docs/walkthroughs/README.md).

**1. Build a curve and confirm it reprices the market.**

```console
$ fit curve build --curve examples/data/usd_treasury.csv --asof 2026-11-17
curve
=====
as_of                     2026-11-17
interpolation           flat-forward
extrapolation           flat-forward
calendar                    weekends
nodes                             11
bootstrap_passes                   1
max_repricing_error_bp    0.00000000

nodes
=====
tenor  type     pillar        years  quote_pct  zero_cc_pct  discount  error_bp
-----  -------  ----------  -------  ---------  -----------  --------  --------
1M     deposit  2026-12-17   0.0822     4.3200       4.3721  0.996413  0.000000
3M     deposit  2027-02-17   0.2521     4.2800       4.3159  0.989181  0.000000
...
30Y    par      2056-11-17  30.0219     4.5800       4.5835  0.252576  0.000000
```

**2. Read rates off it.**

```console
$ fit curve query --curve examples/data/usd_treasury.csv --asof 2026-11-17 --tenors 2Y,5Y,10Y
curve
=====
tenor  date          years  zero_cc_pct  zero_sa_pct  par_sa_pct  fwd_inst_pct  discount
-----  ----------  -------  -----------  -----------  ----------  ------------  --------
2Y     2028-11-17   2.0027       3.8536       3.8909      3.9000        3.7404  0.925726
5Y     2031-11-17   5.0027       3.9119       3.9504      3.9500        4.2187  0.822257
10Y    2036-11-17  10.0082       4.2417       4.2870      4.2500        4.9150  0.654084
```

**3. Analyse a bond offered at 99.9375.**

```console
$ fit bond price --maturity 2036-11-15 --coupon 4.25 --price 99.9375 --face 10000000 \
      --curve examples/data/usd_treasury.csv --asof 2026-11-17
bond
====
settle                2026-11-18
clean_price            99.937500
accrued                 0.035221
dirty_price            99.972721
yield_pct                4.25770
zspread_bp                 0.572
macaulay_duration         8.2407
modified_duration         8.0689
convexity                77.3907
dv01                    0.080667
face               10,000,000.00
settlement_amount   9,997,272.10
position_dv01           8,066.68
```

**4. Value a portfolio.**

```console
$ fit portfolio value --portfolio examples/data/portfolio.csv \
      --curve examples/data/usd_treasury.csv --asof 2026-11-17
positions
=========
id                   face  clean_price  yield_pct  zspread_bp   market_value     accrued  duration       dv01
-------------  ----------  -----------  ---------  ----------  -------------  ----------  --------  ---------
UST-2Y         25,000,000      99.9375     3.9077        0.28  25,029,868.78   45,493.78     1.900   4,755.09
UST-5Y         15,000,000      99.6250     3.9588        0.94  14,971,046.27   27,296.27     4.548   6,808.98
...

totals
======
settle           2026-11-17
positions                 7
face          73,000,000.00
market_value  73,329,137.00
accrued          228,893.68
dv01              43,811.68
duration             5.9747
convexity           70.6371
yield_pct            4.2503
```

**5. Check a package of trades before executing it.**

```console
$ fit pretrade --trades examples/data/trades.csv --portfolio examples/data/portfolio.csv \
      --curve examples/data/usd_treasury.csv --asof 2026-11-17 \
      --key-tenors 2Y,5Y,10Y,30Y --dv01-limit 60000
...
impact
======
metric               before          trade          after         change
------------  -------------  -------------  -------------  -------------
market_value  73,329,137.00  -2,931,463.63  70,397,673.37  -2,931,463.63
dv01              43,811.68       6,662.56      50,474.24       6,662.56
kr_dv01_2Y         6,087.20      -1,703.42       4,383.78      -1,703.42
kr_dv01_5Y        10,190.36       1,244.08      11,434.44       1,244.08
kr_dv01_10Y       18,534.74       2,624.96      21,159.70       2,624.96
kr_dv01_30Y        8,999.37       4,496.94      13,496.31       4,496.94
...
limits
======
dv01_limit       60,000.00
dv01_before      43,811.68
dv01_after       50,474.24
headroom          9,525.76
utilisation_pct       84.1
status                  OK
```

## Command reference

`fit --help` lists commands; `fit <command> --help` lists every option with its default and
an example.

| Command | Purpose |
|---------|---------|
| `fit curve build` | Bootstrap a zero curve; show nodes and repricing errors |
| `fit curve query` | Zero, par and forward rates and discount factors at tenors or dates; `--horizon` for the forward-implied curve |
| `fit curve forwards` | Strip of consecutive forward rates |
| `fit bond price` | Price, yield, accrued, duration, convexity, DV01 from a yield, a price or a curve |
| `fit bond yield` | The same analytics from a clean price |
| `fit bond cashflows` | Remaining cashflows, with discount factors and PVs when a curve is given |
| `fit bond risk` | Effective duration and convexity; key-rate DV01 ladder |
| `fit bond forecast` | Horizon analysis: scenario returns and P&L attribution |
| `fit portfolio value` | Position and portfolio valuation |
| `fit portfolio risk` | Key-rate DV01 ladder by position |
| `fit portfolio scenario` | P&L under parallel shifts and twists |
| `fit portfolio cashflows` | Projected cashflow ladder |
| `fit pretrade` | Before/after analysis of proposed trades; DV01 limit check |

Option groups shared across commands:

| Group | Options |
|-------|---------|
| Curve | `--curve FILE` `--asof DATE` `--interp flat-forward\|linear-zero\|cubic-zero\|monotone-cubic\|monotone-convex` `--extrapolation flat-forward\|flat-zero` `--calendar none\|weekends\|us` `--holidays FILE` `--spot-lag DAYS` |
| Bond | `--maturity DATE` `--coupon PCT` `--frequency 1\|2\|4\|12` `--daycount DC` `--issue DATE` `--settle DATE` `--settle-days N` `--no-eom` |
| Mark | one of `--price PX` (clean), `--yield PCT`, `--zspread BP`; none means "on the curve, no spread" |
| Risk | `--key-tenors LIST` `--bump-bp BP` |
| Scenarios | `--shifts LIST` e.g. `-50,0,50` or twists `2Y:-25/10Y:25` |
| Output | `--format table\|csv\|json` `--precision N` |

Negative values are accepted as written (`--shifts -50,0,50`); `--shifts=-50,0,50` works too.

## Input files

Inputs are CSV files with a header row. Column names are case-insensitive, surrounding
whitespace is ignored, and blank lines and lines starting with `#` are skipped. Unknown
columns are rejected, so a misspelt header is an error rather than a silently ignored field.

**Curve quotes** (`--curve`):

```csv
type,tenor,rate
deposit,3M,4.28
par,2Y,3.90
par,10Y,4.25
```

| Column | Required | Meaning |
|--------|----------|---------|
| `type` | yes | `deposit`, `swap`, `par` or `zero` |
| `tenor` | yes | tenor (`3M`, `10Y`) or maturity date |
| `rate` | yes | quote in percent |
| `daycount`, `frequency`, `settle_days` | no | override the quote type's conventions |

**Positions and trades** (`--portfolio`, `--trades`):

```csv
id,face,maturity,coupon,frequency,daycount,price,zspread
UST-10Y,20000000,2036-11-15,4.250,2,act/act,99.9375,
CORP-A-2032,6000000,2032-03-15,5.100,2,30/360,,85
```

| Column | Required | Meaning |
|--------|----------|---------|
| `id` | yes | identifier |
| `face` | yes | face amount; negative for shorts and sales |
| `maturity`, `coupon` | yes | maturity date; annual coupon in percent |
| `frequency`, `daycount`, `issue` | no | default semi-annual, `act/act`, regular schedule |
| `price` | no | clean market price; marks the position to market |
| `zspread` | no | z-spread in bp for marking to model (default 0); not with `price` |

**Holidays** (`--holidays`): one `YYYY-MM-DD` per line.

## Units and conventions

| Quantity | At the command line and in files | In output |
|----------|----------------------------------|-----------|
| Rates, coupons, yields | percent (`4.25`) | columns ending `_pct` |
| Spreads, shifts, bumps | basis points (`85`) | columns ending `_bp` |
| Prices | per 100 face, clean unless stated | `clean_price`, `dirty_price` |
| Sizes | face amount in currency | `face`, `market_value` |
| DV01 | | per 100 face for a bond; currency for a position |
| Dates, tenors | `YYYY-MM-DD`; `1W`, `3M`, `10Y` | `YYYY-MM-DD` |

Defaults a desk should know (all are in [METHODOLOGY.md](docs/METHODOLOGY.md)):

- A buyer receives the coupons whose nominal date is after settlement; each is discounted
  from its business-day adjusted payment date.
- Bond settlement is T+1 business day from `--asof` unless `--settle` or `--settle-days` is
  given. Portfolio and pre-trade valuation settle on the as-of date unless `--settle` is given.
- Yield is street convention, compounded at the coupon frequency.
- Z-spread is a continuously compounded spread over the zero curve.
- DV01 is positive for a long position: the gain for a 1bp fall in rates.
- Curve risk bumps zero rates at constant z-spread; key-rate DV01s sum to the parallel DV01.
- Quote start dates are the as-of date unless `--spot-lag` (or a `settle_days` column) is set.

## Output formats and exit codes

- `--format table` (default) prints aligned, rounded tables for reading.
- `--format csv` and `--format json` carry full precision (12 significant digits) and are
  the formats to feed other programs. In JSON each table is a named member; key/value tables
  are objects and row tables are arrays of objects.

```console
$ fit bond price --maturity 2036-11-15 --coupon 4.25 --settle 2026-11-18 --yield 4.40 \
      --format json | jq '{clean: .bond.clean_price, dv01: .bond.dv01}'
{
  "clean": 98.7974063813,
  "dv01": 0.079573975602
}
```

Results go to standard output; errors and warnings go to standard error, prefixed
`fit: error:` or `fit: warning:`. A failed command writes nothing to standard output.

| Exit code | Meaning |
|-----------|---------|
| 0 | success |
| 1 | data or runtime error (bad file, invalid date, bond already matured, no convergence) |
| 2 | usage error (unknown command or option, missing or conflicting options) |

## Accuracy and validation

Numerical output is tested against sources independent of this code:

- published Microsoft Excel reference values for price, yield and duration;
- QuantLib, for accrued interest, price, duration and convexity across day counts,
  frequencies and stub periods, for bootstrapped discount factors, and for z-spread pricing,
  agreeing to `1e-9` or better;
- SciPy, for the cubic and monotone cubic interpolators;
- QuantLib's convex monotone interpolation and bootstrap, set to the published Hagan-West
  method, and hand calculations, for the monotone convex interpolator;
- QuantLib's US government bond calendar, day for day from 1990 to 2060;
- exact identities: every curve reprices its inputs, price and yield round-trip, key-rate
  DV01s sum to the parallel DV01, and horizon P&L components sum to the total.

The details and tolerances are in [METHODOLOGY.md](docs/METHODOLOGY.md#9-validation), and
`tools/quantlib_crosscheck.py` re-runs the QuantLib comparison against your build.

## Limitations

v0.1 is an MVP. The main things it does not do, each scheduled in the
[roadmap](docs/ROADMAP.md):

- **Instruments:** fixed-rate bullet and zero-coupon bonds only. No floaters, callables,
  amortisers, inflation-linked bonds or bills on a discount basis.
- **Curves:** one curve for projection and discounting; no futures, no multi-curve OIS
  discounting, no parametric fits. Extrapolation beyond the last pillar is flat (forward or
  zero rate), with no ultimate forward rate.
- **Yields and spreads:** street-convention yield and z-spread only; no G-spread, asset-swap
  spread or OAS.
- **Risk:** sensitivities are to zero-rate bumps, not to par market quotes; no credit spread
  risk, no VaR.
- **Forecasting:** deterministic scenarios at constant spread; no financing leg.
- **Portfolio:** single currency, one settlement date per run, no grouping hierarchy.
- **Calendars:** weekends, a rule-based US government bond calendar, and user holiday files.
- **Data:** file inputs only. The example data is illustrative, not real market data.

`fit` is an analytical tool. It does not execute trades, and its output is not investment
advice; validate it against your own systems before relying on it.

## Documentation

| Document | Contents |
|----------|----------|
| [Walk-throughs](docs/walkthroughs/README.md) | Worked examples of every feature with real output |
| [METHODOLOGY.md](docs/METHODOLOGY.md) | Conventions, formulas, tolerances and validation |
| [Architecture decision records](docs/adr/README.md) | Why the tool is built the way it is |
| [ROADMAP.md](docs/ROADMAP.md) | Planned work by release, known limitations, non-goals |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Development setup, coding and testing standards, how to add features |
| [PR_STANDARDS.md](docs/PR_STANDARDS.md) | Pull request requirements and the numerical-change policy |
| [CHANGELOG.md](CHANGELOG.md) | Release history |

## Project layout

```
include/fit/      public headers of the fit_core library
  core/           error type
  time/           dates, calendars, day counts, schedules
  math/           root-finding, interpolation
  curve/          yield curves, bootstrapping
  bond/           bonds, analytics, horizon analysis
  portfolio/      positions, valuation, aggregation
  io/             CSV input; table, CSV and JSON output
src/              library implementation, by module
src/cli/          the fit executable: argument parsing and commands
tests/unit/       doctest unit tests
tests/cli/        end-to-end command-line tests
examples/data/    illustrative curves, portfolio and trades
docs/             methodology, walk-throughs, ADRs, roadmap, PR standards
tools/            development aids (QuantLib cross-check)
third_party/      vendored doctest header (tests only)
```

All analytics live in the `fit_core` library; the executable only parses arguments and
formats results, so the same functions can be linked into other programs.

## Contributing

Contributions are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) and
[PR_STANDARDS.md](docs/PR_STANDARDS.md) first: in particular, any change to a computed
number needs an independently sourced expected value.

```console
$ cmake --preset dev && cmake --build --preset dev && ctest --preset dev
```

## License

[MIT](LICENSE).

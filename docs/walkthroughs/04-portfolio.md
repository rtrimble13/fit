# Walk-through 4: Portfolio valuation and risk

This walk-through marks a book of bonds, aggregates its interest-rate risk, stresses it and
projects its cashflows.

- [1. The positions file](#1-the-positions-file)
- [2. Valuation](#2-valuation)
- [3. Where the risk is: the key-rate ladder](#3-where-the-risk-is-the-key-rate-ladder)
- [4. Scenario P&L](#4-scenario-pl)
- [5. Cashflow ladder](#5-cashflow-ladder)
- [6. End-of-day reporting](#6-end-of-day-reporting)
- [7. Input errors](#7-input-errors)

## 1. The positions file

A portfolio is a CSV with one row per position.

| Column | Required | Meaning |
|--------|----------|---------|
| `id` | yes | position identifier |
| `face` | yes | face amount; negative for a short |
| `maturity` | yes | maturity date |
| `coupon` | yes | annual coupon in percent |
| `frequency` | no | coupons per year (default 2) |
| `daycount` | no | accrual day count (default `act/act`) |
| `issue` | no | dated date, when the first coupon period is irregular |
| `price` | no | clean market price per 100 |
| `zspread` | no | z-spread in bp, used when no price is given (default 0) |

A row carries a `price` or a `zspread`, not both. Positions with a price are **marked to
market**; positions without one are **marked to model**, off the curve plus their z-spread.
That is the usual split between liquid governments and credit bonds without a reliable
quote.

```console
$ cat examples/data/portfolio.csv
# Illustrative Treasury and credit holdings. face is signed (negative = short).
# price is the clean market price; rows without a price are marked off the curve + zspread (bp).
id,face,maturity,coupon,frequency,daycount,price,zspread
UST-2Y,25000000,2028-10-31,3.875,2,act/act,99.9375,
UST-5Y,15000000,2031-10-31,3.875,2,act/act,99.6250,
UST-10Y,20000000,2036-11-15,4.250,2,act/act,99.9375,
UST-30Y,8000000,2056-11-15,4.625,2,act/act,100.6250,
UST-7Y-SHORT,-5000000,2033-10-31,4.000,2,act/act,99.5000,
CORP-A-2032,6000000,2032-03-15,5.100,2,30/360,,85
CORP-BBB-2035,4000000,2035-06-01,5.750,2,30/360,,140
```

## 2. Valuation

```console
$ fit portfolio value --portfolio examples/data/portfolio.csv --curve examples/data/usd_treasury.csv --asof 2026-11-17
positions
=========
id                   face  clean_price  yield_pct  zspread_bp   market_value     accrued  duration       dv01
-------------  ----------  -----------  ---------  ----------  -------------  ----------  --------  ---------
UST-2Y         25,000,000      99.9375     3.9077        0.28  25,029,868.78   45,493.78     1.900   4,755.09
UST-5Y         15,000,000      99.6250     3.9588        0.94  14,971,046.27   27,296.27     4.548   6,808.98
UST-10Y        20,000,000      99.9375     4.2577        0.57  19,992,196.13    4,696.13     8.239  16,472.20
UST-30Y         8,000,000     100.6250     4.5864        0.66   8,052,044.20    2,044.20    16.366  13,178.23
UST-7Y-SHORT   -5,000,000      99.5000     4.0830        0.45  -4,984,392.27   -9,392.27     6.127  -3,053.99
CORP-A-2032     6,000,000     101.1820     4.8441       85.00   6,123,622.25   52,700.00     4.698   2,876.71
CORP-BBB-2035   4,000,000     100.9674     5.6055      140.00   4,144,751.63  106,055.56     6.694   2,774.47

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

Per position:

- `clean_price` and `yield_pct`: the market price where one was given, otherwise the model
  price; the yield follows from the price.
- `zspread_bp`: for priced positions, the spread *implied* by the price (the Treasuries sit
  within a basis point of the curve); for the others, the input spread.
- `market_value` = face x dirty price / 100. `accrued` is the accrued interest inside it.
- `duration` is effective duration and `dv01` the currency P&L for a 1bp parallel fall in
  zero rates, both from bumping the curve at constant z-spread
  ([walk-through 2, section 6](02-bond-pricing.md#6-curve-risk-and-key-rates)).
  The short position has negative market value and negative DV01.

Totals:

- `dv01` is additive. `duration` is the portfolio DV01 expressed per unit of market value
  (`dv01 x 10000 / market_value`), which is the right number for a book with shorts, where a
  simple weighted average would mislead.
- `convexity` and `yield_pct` are market-value weighted averages.

Positions are valued for settlement on the as-of date unless `--settle` says otherwise, so
that every position is on the same date. Adding `--trades FILE` to any `fit portfolio`
command includes proposed trades in the book (see [walk-through 5](05-pretrade.md)).

## 3. Where the risk is: the key-rate ladder

A single DV01 hides curve exposure. `fit portfolio risk` splits it by tenor for each
position and in total.

```console
$ fit portfolio risk --portfolio examples/data/portfolio.csv --curve examples/data/usd_treasury.csv --asof 2026-11-17 --key-tenors 2Y,5Y,10Y,30Y | head -12
key_rate_dv01
=============
id                2Y      5Y     10Y    30Y   total
-------------  -----  ------  ------  -----  ------
UST-2Y         4,755       0       0      0   4,755
UST-5Y           430   6,379       0      0   6,809
UST-10Y          497   1,506  14,463      5  16,472
UST-30Y          216     656   3,312  8,994  13,178
UST-7Y-SHORT    -117  -1,860  -1,076      0  -3,054
CORP-A-2032      176   2,533     167      0   2,877
CORP-BBB-2035    130     976   1,668      0   2,774
TOTAL          6,087  10,190  18,535  8,999  43,812
```

Each row sums to its position's DV01 and each column to the book's exposure at that tenor.
The book is long everywhere, with the largest exposure (18,535 per bp) at the 10Y point. The
short 7Y position shows up as negative entries split between the 5Y and 10Y buckets either
side of it.

The default ladder is finer (`3M,6M,1Y,2Y,3Y,5Y,7Y,10Y,20Y,30Y`); choose the tenors of the
instruments you would actually hedge with.

## 4. Scenario P&L

`fit portfolio scenario` reprices every position under instantaneous curve shocks, holding
each z-spread constant. Shocks use the same syntax as
[bond forecasts](03-forecasting.md#4-rate-scenarios): parallel shifts in bp, or twists as
`tenor:bp` pairs.

```console
$ fit portfolio scenario --portfolio examples/data/portfolio.csv --curve examples/data/usd_treasury.csv --asof 2026-11-17 --shifts=-100,-50,-25,25,50,100,2Y:-25/10Y:25,2Y:25/10Y:-25
scenarios
=========
scenario        market_value            pnl  pnl_pct
-------------  -------------  -------------  -------
base           73,329,137.00           0.00    0.000
-100bp         77,986,740.15   4,657,603.15    6.352
-50bp          75,586,580.50   2,257,443.50    3.079
-25bp          74,440,875.46   1,111,738.46    1.516
25bp           72,249,779.84  -1,079,357.16   -1.472
50bp           71,201,312.43  -2,127,824.57   -2.902
100bp          69,191,512.91  -4,137,624.09   -5.643
2Y:-25/10Y:25  72,870,798.85    -458,338.15   -0.625
2Y:25/10Y:-25  73,816,254.01     487,117.01    0.664
```

Reading the table against the risk numbers:

- A 25bp rise costs about 1.08 million. The first-order estimate is 25 x 43,812 = 1.10
  million; full repricing gives a slightly smaller loss because of convexity. The same
  effect makes the -100bp gain (4.66 million) larger than the +100bp loss (4.14 million).
- The steepener `2Y:-25/10Y:25` loses 0.46 million although the average move is zero,
  because the book's risk is concentrated beyond the 6Y pivot. The key-rate ladder predicts
  this: the 10Y and 30Y buckets together outweigh the 2Y bucket.

Scenario P&L is exact repricing, not a duration approximation, so it remains reliable for
large moves.

## 5. Cashflow ladder

`fit portfolio cashflows` projects coupons and principal, netting longs and shorts.
`--bucket` groups by `year` (default), `month`, or `none` for individual payment dates.

```console
$ fit portfolio cashflows --portfolio examples/data/portfolio.csv --curve examples/data/usd_treasury.csv --asof 2026-11-17 --bucket year | head -14
cashflows
=========
year          coupon      principal          total       curve_pv
-----  -------------  -------------  -------------  -------------
2026      115,000.00           0.00     115,000.00     114,807.31
2027    3,106,000.00           0.00   3,106,000.00   3,017,046.51
2028    3,106,000.00  25,000,000.00  28,106,000.00  26,089,293.21
2029    2,137,250.00           0.00   2,137,250.00   1,925,691.15
2030    2,137,250.00           0.00   2,137,250.00   1,850,899.55
2031    2,137,250.00  15,000,000.00  17,137,250.00  14,134,630.05
2032    1,403,000.00   6,000,000.00   7,403,000.00   5,981,234.48
2033    1,250,000.00  -5,000,000.00  -3,750,000.00  -2,820,930.37
2034    1,450,000.00           0.00   1,450,000.00   1,054,082.91
2035    1,335,000.00   4,000,000.00   5,335,000.00   3,729,454.57
```

`curve_pv` discounts each flow on the curve with no spread, a quick view of how much of the
book's value sits in each period. 2033 shows a net outflow: the short 7Y bond's redemption.

```console
$ fit portfolio cashflows --portfolio examples/data/portfolio.csv --curve examples/data/usd_treasury.csv --asof 2026-11-17 --bucket month | head -8
cashflows
=========
month           coupon      principal          total       curve_pv
-------  -------------  -------------  -------------  -------------
2026-12     115,000.00           0.00     115,000.00     114,807.31
2027-03     153,000.00           0.00     153,000.00     150,902.33
2027-04     675,000.00           0.00     675,000.00     662,307.22
2027-05     610,000.00           0.00     610,000.00     597,385.22
```

## 6. End-of-day reporting

With `--format csv` each table is preceded by a `# name` line, and with `--format json` the
tables become named members of one object, so reports load directly into other tools.

```console
$ fit portfolio value --portfolio examples/data/portfolio.csv --curve examples/data/usd_treasury.csv --asof 2026-11-17 --format csv
# positions
id,face,clean_price,yield_pct,zspread_bp,market_value,accrued,duration,dv01
UST-2Y,25000000,99.9375,3.90769549654,0.282285631701,25029868.7845,45493.7845304,1.89976519087,4755.08734488
UST-5Y,15000000,99.625,3.95875916274,0.935587524027,14971046.2707,27296.2707182,4.54809917468,6808.9803188
UST-10Y,20000000,99.9375,4.25771322215,0.569043475865,19992196.1326,4696.13259669,8.23931324497,16472.1966391
UST-30Y,8000000,100.625,4.58642155323,0.656770280789,8052044.1989,2044.19889503,16.3663101565,13178.2252753
UST-7Y-SHORT,-5000000,99.5,4.08304182707,0.451267858323,-4984392.26519,-9392.26519337,6.12710702331,-3053.9904855
CORP-A-2032,6000000,101.182037431,4.84413459651,85,6123622.24586,52700,4.6977253157,2876.70952482
CORP-BBB-2035,4000000,100.967401894,5.60546077971,140,4144751.63131,106055.555556,6.69394213639,2774.47275897

# totals
key,value
settle,2026-11-17
positions,7
face,73000000
market_value,73329136.9987
accrued,228893.677103
dv01,43811.6813764
duration,5.97466207426
convexity,70.6371457433
yield_pct,4.25032126037
```

```console
$ fit portfolio value --portfolio examples/data/portfolio.csv --curve examples/data/usd_treasury.csv --asof 2026-11-17 --format json | jq '.totals'
{
  "settle": "2026-11-17",
  "positions": 7,
  "face": 73000000,
  "market_value": 73329136.9987,
  "accrued": 228893.677103,
  "dv01": 43811.6813764,
  "duration": 5.97466207426,
  "convexity": 70.6371457433,
  "yield_pct": 4.25032126037
}
```

```console
$ fit portfolio value --portfolio examples/data/portfolio.csv --curve examples/data/usd_treasury.csv --asof 2026-11-17 --format json | jq -r '.positions | sort_by(-.dv01)[:3][] | "\(.id)\t\(.dv01|round)"'
UST-10Y	16472
UST-30Y	13178
UST-5Y	6809
```

A daily run is one command per report; when any input is bad the command exits non-zero and
prints nothing on standard output, so a failed run cannot be mistaken for a report.

## 7. Input errors

Errors name the file, the line and the problem.

```console
$ printf 'id,face,maturity,coupon,price,zspread\nA,1000000,2030-05-15,4.0,99.5,50\n' > /tmp/p.csv; fit portfolio value --portfolio /tmp/p.csv --curve examples/data/usd_treasury.csv --asof 2026-11-17
fit: error: /tmp/p.csv:2: give either 'price' or 'zspread', not both
```

(exit code 1)

```console
$ printf 'id,face,maturity,cupon\nA,1000000,2030-05-15,4.0\n' > /tmp/p.csv; fit portfolio value --portfolio /tmp/p.csv --curve examples/data/usd_treasury.csv --asof 2026-11-17
fit: error: /tmp/p.csv: unknown column 'cupon' (expected: id, face, maturity, coupon, frequency, daycount, issue, price, zspread)
```

(exit code 1)

```console
$ printf 'id,face,maturity,coupon\nOLD,1000000,2025-05-15,4.0\n' > /tmp/p.csv; fit portfolio value --portfolio /tmp/p.csv --curve examples/data/usd_treasury.csv --asof 2026-11-17
fit: error: position 'OLD': bond 'OLD' has no cashflows after 2026-11-17 (matures 2025-05-15)
```

(exit code 1)

Next: [pre-trade analysis](05-pretrade.md).

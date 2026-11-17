# Walk-through 5: Pre-trade analysis

Before a trade goes to the market a desk wants to know three things: what it costs, what it
does to the book's risk, and whether the book stays inside its limits. This walk-through
covers a single-ticket check, a multi-leg what-if against the portfolio, limit checking and
sizing a hedge.

- [1. Single-ticket check](#1-single-ticket-check)
- [2. The trades file](#2-the-trades-file)
- [3. What-if against the portfolio](#3-what-if-against-the-portfolio)
- [4. Limit checks](#4-limit-checks)
- [5. Sizing a duration-neutral switch](#5-sizing-a-duration-neutral-switch)
- [6. Stress-testing the proposed book](#6-stress-testing-the-proposed-book)

## 1. Single-ticket check

A dealer offers 4 million of the 4.625% November 2056 bond at 100-20 (100.625). One command
gives the yield, the richness against our curve, the cash needed and the risk added:

```console
$ fit bond price --maturity 2056-11-15 --coupon 4.625 --curve examples/data/usd_treasury.csv --asof 2026-11-17 --price 100.625 --face 4000000
bond
====
settle               2026-11-18
clean_price          100.625000
accrued                0.038329
dirty_price          100.663329
yield_pct               4.58641
zspread_bp                0.653
macaulay_duration       16.5442
modified_duration       16.1733
convexity              377.4092
dv01                   0.162806
face               4,000,000.00
settlement_amount  4,026,533.15
position_dv01          6,512.25
```

The bond yields 4.586%, sits 0.7bp cheap to the curve, costs 4,026,533.15 for T+1 settlement
and adds about 6,512 of DV01 measured on yield.

What is the same offer worth a quarter-point lower in yield? Price it from the yield instead:

```console
$ fit bond price --maturity 2056-11-15 --coupon 4.625 --settle 2026-11-18 --yield 4.336 --face 4000000 | sed -n 3,5p
settle               2026-11-18
clean_price          104.823660
accrued                0.038329
```

A 25bp lower yield is worth about 4.2 points on this bond, which is the scale against which
to judge a negotiation over a 32nd.

## 2. The trades file

Proposed trades use the [positions schema](04-portfolio.md#1-the-positions-file). `face` is
signed: positive buys, negative sells. A trade in a bond already held is simply another row;
it does not need to match an existing `id`.

```console
$ cat examples/data/trades.csv
# Proposed trades: extend duration by selling 2Y and buying 30Y, and add a new corporate.
id,face,maturity,coupon,frequency,daycount,price,zspread
UST-2Y,-10000000,2028-10-31,3.875,2,act/act,99.9375,
UST-30Y,4000000,2056-11-15,4.625,2,act/act,100.6250,
CORP-A-2034,3000000,2034-09-15,5.250,2,30/360,,95
```

## 3. What-if against the portfolio

`fit pretrade` values the existing book, the trades and the combined book on the same curve
and date.

```console
$ fit pretrade --trades examples/data/trades.csv --portfolio examples/data/portfolio.csv --curve examples/data/usd_treasury.csv --asof 2026-11-17 --key-tenors 2Y,5Y,10Y,30Y
trades
======
id           side        face  clean_price  accrued  yield_pct  zspread_bp  settlement_amount  duration       dv01
-----------  ----  ----------  -----------  -------  ---------  ----------  -----------------  --------  ---------
UST-2Y       SELL  10,000,000      99.9375   0.1820     3.9077        0.28     -10,011,947.51     1.900  -1,902.03
UST-30Y      BUY    4,000,000     100.6250   0.0256     4.5864        0.66       4,026,022.10    16.366   6,589.11
CORP-A-2034  BUY    3,000,000     100.9112   0.9042     5.1062       95.00       3,054,461.79     6.468   1,975.48

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

ratios
======
metric      before    after   change
---------  -------  -------  -------
duration    5.9747   7.1699   1.1952
convexity  70.6371  96.8900  26.2528
yield_pct   4.2503   4.3554   0.1051
```

**`trades`** is the ticket view. `settlement_amount` is face x dirty price / 100, positive
for cash paid and negative for cash received. The corporate bond has no market price, so it
is valued at its 95bp z-spread; its `clean_price` is the model's view of fair value at that
spread, the level to compare a dealer's offer against.

**`impact`** shows each additive measure before, for the trades alone, and after:

- The package releases 2.93 million of cash (`market_value` change): the 2Y sale more than
  funds the two purchases.
- DV01 rises by 6,663 to 50,474 even though cash is taken out, because the 30Y bond carries
  more than eight times the risk of the 2Y per unit of face.
- The key-rate rows show *where*: risk leaves the 2Y bucket (-1,703) and arrives mostly at
  30Y (+4,497) and 10Y (+2,625). This is a duration extension and a curve steepening
  exposure, not just "more risk".

**`ratios`** are not additive, so they have no trade column: duration lengthens from 5.97 to
7.17 and the book's yield rises by 11bp.

Like `portfolio value`, `pretrade` values everything for settlement on the as-of date unless
`--settle` is given. Use `fit bond price` (section 1) for the exact cash on the bond's own
settlement date.

## 4. Limit checks

`--dv01-limit` compares the absolute DV01 of the combined book with a limit.

```console
$ fit pretrade --trades examples/data/trades.csv --portfolio examples/data/portfolio.csv --curve examples/data/usd_treasury.csv --asof 2026-11-17 --dv01-limit 60000 | tail -8
limits
======
dv01_limit       60,000.00
dv01_before      43,811.68
dv01_after       50,474.24
headroom          9,525.76
utilisation_pct       84.1
status                  OK
```

With a tighter limit the status becomes `BREACH` and a warning is written to standard error.
The exit code stays 0, because the analysis itself succeeded; scripts should read the
`status` field.

```console
$ fit pretrade --trades examples/data/trades.csv --portfolio examples/data/portfolio.csv --curve examples/data/usd_treasury.csv --asof 2026-11-17 --dv01-limit 48000 | tail -8
limits
======
dv01_limit       48,000.00
dv01_before      43,811.68
dv01_after       50,474.24
headroom         -2,474.24
utilisation_pct      105.2
status              BREACH
```

```console
$ fit pretrade --trades examples/data/trades.csv --portfolio examples/data/portfolio.csv --curve examples/data/usd_treasury.csv --asof 2026-11-17 --dv01-limit 48000 --format json 2>/dev/null | jq -r '.limits.status'
BREACH
```

## 5. Sizing a duration-neutral switch

Suppose the aim is to buy the 4 million 30Y bonds without adding DV01, by selling 10Y bonds
against them. Trades can be analysed on their own by omitting `--portfolio`:

```console
$ fit pretrade --trades examples/data/trades.csv --curve examples/data/usd_treasury.csv --asof 2026-11-17 | sed -n 1,6p
trades
======
id           side        face  clean_price  accrued  yield_pct  zspread_bp  settlement_amount  duration       dv01
-----------  ----  ----------  -----------  -------  ---------  ----------  -----------------  --------  ---------
UST-2Y       SELL  10,000,000      99.9375   0.1820     3.9077        0.28     -10,011,947.51     1.900  -1,902.03
UST-30Y      BUY    4,000,000     100.6250   0.0256     4.5864        0.66       4,026,022.10    16.366   6,589.11
```

Per unit of face the DV01s are in the `trades` table of any run. The 4 million of 30Y has a
DV01 of 6,589 and the portfolio report in [walk-through 4](04-portfolio.md#2-valuation) shows
16,472 for 20 million of the 10Y, or 824 per million. The hedge ratio is therefore
6,589 / 824 = 8.0 million of 10Y to sell. `examples/data/switch.csv` holds that package:

```console
$ cat examples/data/switch.csv
# A duration-neutral switch: buy 30Y against a DV01-weighted sale of 10Y.
id,face,maturity,coupon,price
UST-30Y,4000000,2056-11-15,4.625,100.625
UST-10Y,-8000000,2036-11-15,4.250,99.9375
```

```console
$ fit pretrade --trades examples/data/switch.csv --portfolio examples/data/portfolio.csv --curve examples/data/usd_treasury.csv --asof 2026-11-17 --key-tenors 2Y,5Y,10Y,30Y | sed -n '/^impact/,$p'
impact
======
metric               before          trade          after         change
------------  -------------  -------------  -------------  -------------
market_value  73,329,137.00  -3,970,856.35  69,358,280.65  -3,970,856.35
dv01              43,811.68           0.23      43,811.92           0.23
kr_dv01_2Y         6,087.20         -90.59       5,996.61         -90.59
kr_dv01_5Y        10,190.36        -274.72       9,915.64        -274.72
kr_dv01_10Y       18,534.74      -4,129.19      14,405.55      -4,129.19
kr_dv01_30Y        8,999.37       4,494.74      13,494.11       4,494.74

ratios
======
metric      before    after   change
---------  -------  -------  -------
duration    5.9747   6.3168   0.3421
convexity  70.6371  87.9551  17.3179
yield_pct   4.2503   4.2690   0.0187
```

Total DV01 is unchanged (the `change` on the `dv01` row is 0.23), but the key-rate rows show
what the switch really is: about 4,100 of DV01 leaves the 10Y point and 4,500 arrives at the
30Y point. The book is neutral to parallel moves and has taken on a 10s30s flattening
position. A single duration number would have reported "no change"; note that portfolio
duration nevertheless *rises*, because the same DV01 now sits on less market value.

## 6. Stress-testing the proposed book

Every `fit portfolio` command accepts `--trades` to include proposed trades, so the
post-trade book can be stressed before anything is executed. First the book as it stands,
then with the switch:

```console
$ fit portfolio scenario --portfolio examples/data/portfolio.csv --curve examples/data/usd_treasury.csv --asof 2026-11-17 --shifts=-50,50,10Y:-10/30Y:10,10Y:10/30Y:-10
scenarios
=========
scenario         market_value            pnl  pnl_pct
--------------  -------------  -------------  -------
base            73,329,137.00           0.00    0.000
-50bp           75,586,580.50   2,257,443.50    3.079
50bp            71,201,312.43  -2,127,824.57   -2.902
10Y:-10/30Y:10  73,589,466.23     260,329.23    0.355
10Y:10/30Y:-10  73,073,221.11    -255,915.89   -0.349
```

```console
$ fit portfolio scenario --portfolio examples/data/portfolio.csv --trades examples/data/switch.csv --curve examples/data/usd_treasury.csv --asof 2026-11-17 --shifts=-50,50,10Y:-10/30Y:10,10Y:10/30Y:-10
scenarios
=========
scenario         market_value            pnl  pnl_pct
--------------  -------------  -------------  -------
base            69,358,280.65           0.00    0.000
-50bp           71,627,988.94   2,269,708.29    3.272
50bp            67,241,262.77  -2,117,017.88   -3.052
10Y:-10/30Y:10  69,528,981.22     170,700.57    0.246
10Y:10/30Y:-10  69,192,532.83    -165,747.82   -0.239
```

Parallel moves produce nearly the same P&L with and without the switch, as intended. A 20bp
10s30s steepening (`10Y:-10/30Y:10`) does not: it earns 260,000 on the current book and
171,000 after the switch. The 90,000 given up is what the key-rate changes in section 5
predict (roughly 4,100 x 10 at the 10Y point plus 4,500 x 10 at the 30Y point), now confirmed
by full repricing before the trade is done.

This completes the walk-throughs. For formulas and conventions see
[METHODOLOGY.md](../METHODOLOGY.md); for what is planned next see [ROADMAP.md](../ROADMAP.md).

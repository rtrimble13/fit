# Walk-through 3: Forecasting with horizon analysis

Yield to maturity says what a bond earns if held to maturity and every coupon is reinvested
at that same yield. A desk usually wants something else: *what do I make over the next three
or twelve months under a given rate scenario?* `fit bond forecast` answers that by projecting
the bond to a horizon date and attributing the return.

- [1. The base case: an unchanged curve](#1-the-base-case-an-unchanged-curve)
- [2. Reading the attribution](#2-reading-the-attribution)
- [3. If the forwards are realised](#3-if-the-forwards-are-realised)
- [4. Rate scenarios](#4-rate-scenarios)
- [5. Curve twists](#5-curve-twists)
- [6. Finding the breakeven](#6-finding-the-breakeven)
- [7. Reinvestment and horizon choices](#7-reinvestment-and-horizon-choices)
- [8. Comparing bonds](#8-comparing-bonds)

## 1. The base case: an unchanged curve

We hold the 4.25% November 2036 bond bought at 99.9375 and look one year ahead.
`--horizon` is a date, or a tenor measured from settlement.

```console
$ fit bond forecast --maturity 2036-11-15 --coupon 4.25 --curve examples/data/usd_treasury.csv --asof 2026-11-17 --price 99.9375 --horizon 1Y
start
=====
settle              2026-11-18
horizon             2027-11-18
horizon_days               365
scenario             unchanged
clean_price          99.937500
accrued               0.035221
dirty_price          99.972721
yield_pct              4.25770
zspread_bp               0.572
coupons_to_horizon    4.250000
accrued_at_horizon    0.035027

scenarios
=========
shift  clean_end  yield_end_pct  coupon_income    roll  curve_shift  reinvestment  total_pnl  return_pct  annualized_pct
-----  ---------  -------------  -------------  ------  -----------  ------------  ---------  ----------  --------------
0bp     100.2641         4.2144         4.2498  0.3266       0.0000        0.0420     4.6185       4.620           4.620
```

The `start` table is today's position. `zspread_bp` is the bond's spread to the curve at the
purchase price; the forecast holds it constant, so the bond stays as rich or cheap as it is
today. Everything is per 100 face.

`--scenario unchanged` (the default) assumes that in a year the curve looks exactly as it
does today: the 9-year zero rate then is today's 9-year zero rate. The bond will then *be* a
9-year bond and is revalued at 9-year rates.

## 2. Reading the attribution

The four components always add up to `total_pnl`:

| Component | Definition | Here |
|-----------|------------|------|
| `coupon_income` | coupons received plus the change in accrued interest | 4.2498 |
| `roll` | clean price change if the horizon curve is as assumed, before any shock | 0.3266 |
| `curve_shift` | further clean price change caused by the shock | 0.0000 |
| `reinvestment` | interest earned on coupons from their payment date to the horizon | 0.0420 |

`roll` is the price gain from *rolling down the curve*: the 9-year point yields less than the
10-year point (4.21% against 4.26%), so a bond that ages one year on an unchanged curve is
discounted at lower rates and its price rises from 99.94 to 100.26.

`return_pct` is `total_pnl` divided by the dirty price paid; `annualized_pct` restates it as a
compound annual rate. The base-case return of 4.62% is more than the 4.26% yield, and the
difference is the roll-down.

## 3. If the forwards are realised

The opposite benchmark is `--scenario forward`: rates move exactly as today's forward curve
predicts (see [walk-through 1, section 6](01-yield-curves.md#6-the-forward-implied-curve)).

```console
$ fit bond forecast --maturity 2036-11-15 --coupon 4.25 --curve examples/data/usd_treasury.csv --asof 2026-11-17 --price 99.9375 --horizon 1Y --scenario forward | tail -4
=========
shift  clean_end  yield_end_pct  coupon_income     roll  curve_shift  reinvestment  total_pnl  return_pct  annualized_pct
-----  ---------  -------------  -------------  -------  -----------  ------------  ---------  ----------  --------------
0bp      99.7377         4.2854         4.2498  -0.1998       0.0000        0.0420     4.0921       4.093           4.093
```

Now `roll` is negative: forwards price in higher long rates, the price falls to 99.74, and the
total return drops to 4.09%. That is no accident. When forwards are realised, *every* bond on
the curve earns the same return over the horizon, namely the one-year rate (about 4.09%
here) plus its spread. This is the no-arbitrage breakeven: the extra yield and roll-down of a
longer bond are exactly what the forwards say will be given back in price.

The two scenarios bracket the useful question. The difference between them, 0.53 points
here, is what you earn if the curve does *not* follow the forwards. Holding this bond rather
than rolling one-year paper is a bet that rates rise by less than the forwards imply.

## 4. Rate scenarios

`--shifts` applies parallel shocks, in basis points, to the horizon curve. Several can be
listed. (Write `--shifts=-50,...` with an equals sign, or put a non-negative value first, so
that a leading minus is not mistaken for an option.)

```console
$ fit bond forecast --maturity 2036-11-15 --coupon 4.25 --curve examples/data/usd_treasury.csv --asof 2026-11-17 --price 99.9375 --horizon 1Y --shifts=-100,-50,-25,0,25,50,100 | tail -9
shift   clean_end  yield_end_pct  coupon_income    roll  curve_shift  reinvestment  total_pnl  return_pct  annualized_pct
------  ---------  -------------  -------------  ------  -----------  ------------  ---------  ----------  --------------
-100bp   108.1831         3.1957         4.2498  0.3266       7.9190        0.0420    12.5374      12.541          12.541
-50bp    104.1400         3.7044         4.2498  0.3266       3.8759        0.0420     8.4944       8.497           8.497
-25bp    102.1816         3.9592         4.2498  0.3266       1.9175        0.0420     6.5360       6.538           6.538
0bp      100.2641         4.2144         4.2498  0.3266       0.0000        0.0420     4.6185       4.620           4.620
25bp      98.3867         4.4698         4.2498  0.3266      -1.8775        0.0420     2.7410       2.742           2.742
50bp      96.5483         4.7256         4.2498  0.3266      -3.7158        0.0420     0.9027       0.903           0.903
100bp     92.9858         5.2381         4.2498  0.3266      -7.2783        0.0420    -2.6599      -2.661          -2.661
```

Only `curve_shift` changes from row to row. Two things to notice:

- The table is asymmetric: +100bp costs 7.28 points of price while -100bp adds 7.92. That is
  convexity working in the holder's favour.
- Carry is a cushion. After a 50bp rise the bond has lost 3.72 points of price but still
  returns +0.90% over the year.

## 5. Curve twists

A shock can also be a piecewise-linear function of tenor, written as `tenor:bp` pairs joined
by `/`. The shift is interpolated linearly between the tenors given and held flat outside
them.

```console
$ fit bond forecast --maturity 2036-11-15 --coupon 4.25 --curve examples/data/usd_treasury.csv --asof 2026-11-17 --price 99.9375 --horizon 1Y --shifts 0,2Y:-25/10Y:25,2Y:25/10Y:-25,5Y:0/30Y:40 | tail -6
shift          clean_end  yield_end_pct  coupon_income    roll  curve_shift  reinvestment  total_pnl  return_pct  annualized_pct
-------------  ---------  -------------  -------------  ------  -----------  ------------  ---------  ----------  --------------
0bp             100.2641         4.2144         4.2498  0.3266       0.0000        0.0420     4.6185       4.620           4.620
2Y:-25/10Y:25    99.1138         4.3702         4.2498  0.3266      -1.1503        0.0420     3.4681       3.469           3.469
2Y:25/10Y:-25   101.4355         4.0578         4.2498  0.3266       1.1713        0.0420     5.7898       5.791           5.791
5Y:0/30Y:40      99.8350         4.2722         4.2498  0.3266      -0.4292        0.0420     4.1893       4.190           4.190
```

- `2Y:-25/10Y:25` is a steepener pivoting around 6Y: bad for a bond that will be a 9-year.
- `2Y:25/10Y:-25` is the mirror-image flattener.
- `5Y:0/30Y:40` leaves the curve alone out to 5Y and sells off the long end.

## 6. Finding the breakeven

How far can rates rise over the year before the position loses money? Scan a few shifts
and read off where `total_pnl` changes sign:

```console
$ fit bond forecast --maturity 2036-11-15 --coupon 4.25 --curve examples/data/usd_treasury.csv --asof 2026-11-17 --price 99.9375 --horizon 1Y --shifts 55,60,65,70 --format csv | grep -A5 '^shift' | cut -d, -f1,2,8,9
shift,clean_end,total_pnl,return_pct
55bp,96.1852967377,0.539635255363,0.539782502661
60bp,95.8237816234,0.178120141013,0.178168743674
65bp,95.4637855546,-0.181875927756,-0.181925555238
70bp,95.1053019333,-0.540359549065,-0.540506993997
```

The breakeven is a parallel rise of about 62bp on top of an unchanged curve. Scripted
workflows can do the same with `--format json` and a root-finder.

## 7. Reinvestment and horizon choices

By default each coupon is reinvested from its payment date to the horizon at the forward rate
implied by today's curve. `--reinvest` fixes the rate instead (in percent, compounded at the
coupon frequency); `--reinvest 0` assumes coupons sit in cash.

```console
$ fit bond forecast --maturity 2036-11-15 --coupon 4.25 --curve examples/data/usd_treasury.csv --asof 2026-11-17 --price 99.9375 --horizon 2029-11-15 --reinvest 3.0 | tail -4
=========
shift  clean_end  yield_end_pct  coupon_income    roll  curve_shift  reinvestment  total_pnl  return_pct  annualized_pct
-----  ---------  -------------  -------------  ------  -----------  ------------  ---------  ----------  --------------
0bp     100.9748         4.0885        12.7148  1.0373       0.0000        0.4886    14.2407      14.245           4.548
```

Over this three-year horizon the bond pays six coupons (the one paid on the horizon date
counts as received), which earn 0.49 of reinvestment income at 3%. The total return of 14.25%
annualises to 4.55%.

Short horizons work the same way and are the natural input to a carry-and-roll comparison:

```console
$ fit bond forecast --maturity 2036-11-15 --coupon 4.25 --curve examples/data/usd_treasury.csv --asof 2026-11-17 --price 99.9375 --horizon 3M | tail -4
=========
shift  clean_end  yield_end_pct  coupon_income    roll  curve_shift  reinvestment  total_pnl  return_pct  annualized_pct
-----  ---------  -------------  -------------  ------  -----------  ------------  ---------  ----------  --------------
0bp     100.0033         4.2489         1.0801  0.0658       0.0000        0.0000     1.1459       1.146           4.625
```

Three months of carry and roll are worth 1.15 points, or 4.63% annualised.

## 8. Comparing bonds

Running the same forecast across maturities shows where the curve pays best for an unchanged
view. Each bond here is priced flat to the curve.

```console
$ for m in 2028-11-15 2031-11-15 2036-11-15 2046-11-15 2056-11-15; do fit bond forecast --maturity $m --coupon 4.25 --curve examples/data/usd_treasury.csv --asof 2026-11-17 --horizon 1Y --format json | jq -r --arg m $m '.scenarios[0] | "\($m)  income \(.coupon_income|.*100|round/100)  roll \(.roll|.*100|round/100)  return \(.return_pct|.*100|round/100)%"'; done
2028-11-15  income 4.25  roll -0.48  return 3.78%
2031-11-15  income 4.25  roll -0.14  return 4.09%
2036-11-15  income 4.25  roll 0.32  return 4.61%
2046-11-15  income 4.25  roll 0.38  return 4.91%
2056-11-15  income 4.25  roll 0.04  return 4.58%
```

On this curve the 20-year sector offers the most roll-down, because the curve is steepest
between 10 and 20 years. The 2-year and 5-year bonds have *negative* roll: the front of the
curve is inverted, so they age into higher-yielding points and lose price. Repeat with `--scenario forward` and every line returns the same
4.09%: by construction no maturity is favoured when forwards are realised.

The assumptions behind these numbers, and what they leave out (financing, spread changes,
default), are set out in [METHODOLOGY.md](../METHODOLOGY.md#6-horizon-analysis).

Next: [valuing a portfolio](04-portfolio.md).

# Walk-through 2: Bond pricing and risk

This walk-through prices a bond three ways (from a yield, from a price, and off a curve),
lists its cashflows, and measures its interest-rate risk.

The running example is a 4.25% semi-annual Treasury-style bond maturing 15 November 2036,
traded on 17 November 2026 for settlement the next business day.

- [1. Price from yield](#1-price-from-yield)
- [2. Yield from price](#2-yield-from-price)
- [3. Pricing off the curve](#3-pricing-off-the-curve)
- [4. Rich or cheap: the z-spread](#4-rich-or-cheap-the-z-spread)
- [5. Cashflows](#5-cashflows)
- [6. Curve risk and key rates](#6-curve-risk-and-key-rates)
- [7. Other conventions: corporates, stubs and zeros](#7-other-conventions-corporates-stubs-and-zeros)

## 1. Price from yield

A bond is described by `--maturity` and `--coupon`; frequency defaults to semi-annual and day
count to ACT/ACT (ICMA), the government bond conventions.

```console
$ fit bond price --maturity 2036-11-15 --coupon 4.25 --settle 2026-11-18 --yield 4.40
bond
====
settle             2026-11-18
clean_price         98.797406
accrued              0.035221
dirty_price         98.832627
yield_pct             4.40000
macaulay_duration      8.2285
modified_duration      8.0514
convexity             77.1345
dv01                 0.079574
```

| Field | Meaning |
|-------|---------|
| `clean_price` | quoted price per 100 face, excluding accrued interest |
| `accrued` | interest accrued since the last coupon date (3 days here) |
| `dirty_price` | what is actually paid: clean plus accrued |
| `yield_pct` | yield to maturity, street convention, compounded at the coupon frequency |
| `macaulay_duration` | present-value-weighted average time to cashflows, in years |
| `modified_duration` | percentage price change for a 1% change in yield |
| `convexity` | second-order yield sensitivity |
| `dv01` | price change per 100 face for a 1bp fall in yield |

A quick sanity check on any duration figure: a 1bp move should change the price by about
`modified_duration x dirty_price / 10000` = 8.0514 x 98.83 / 10000 = 0.0796, which is `dv01`.

## 2. Yield from price

Traders quote price, so the usual direction is the other way. `--price` is the clean price.
`--face` adds the cash settlement amount and position DV01 for a given size.

```console
$ fit bond yield --maturity 2036-11-15 --coupon 4.25 --settle 2026-11-18 --price 99.9375 --face 10000000
bond
====
settle                2026-11-18
clean_price            99.937500
accrued                 0.035221
dirty_price            99.972721
yield_pct                4.25770
macaulay_duration         8.2407
modified_duration         8.0689
convexity                77.3907
dv01                    0.080667
face               10,000,000.00
settlement_amount   9,997,272.10
position_dv01           8,066.68
```

Buying 10 million face costs 9,997,272.10 including 3,522.10 of accrued interest, and the
position gains or loses about 8,067 for each basis point move in yield.

Instead of `--settle` you can give the trade date with `--asof`; settlement is then
`--settle-days` business days later (default 1):

```console
$ fit bond yield --maturity 2036-11-15 --coupon 4.25 --asof 2026-11-20 --price 99.9375 | head -3
bond
====
settle             2026-11-23
```

20 November 2026 is a Friday, so T+1 settlement is Monday the 23rd.

## 3. Pricing off the curve

Supplying a curve (see [walk-through 1](01-yield-curves.md)) and no price or yield values the
bond by discounting each cashflow on the curve.

```console
$ fit bond price --maturity 2036-11-15 --coupon 4.25 --curve examples/data/usd_treasury.csv --asof 2026-11-17
bond
====
settle             2026-11-18
clean_price         99.984619
accrued              0.035221
dirty_price        100.019840
yield_pct             4.25186
zspread_bp              0.000
macaulay_duration      8.2412
modified_duration      8.0696
convexity             77.4012
dv01                 0.080712
```

The curve's 10Y par yield is 4.25%, and this 4.25% bond with ten years to run prices at
almost exactly par: the model price is consistent with the quotes the curve was built from.
`zspread_bp` is 0 because the bond was priced flat to the curve.

To price at a spread, as for a credit bond, add `--zspread` in basis points:

```console
$ fit bond price --maturity 2036-11-15 --coupon 4.25 --curve examples/data/usd_treasury.csv --asof 2026-11-17 --zspread 85 | head -8
bond
====
settle             2026-11-18
clean_price         93.251094
accrued              0.035221
dirty_price         93.286315
yield_pct             5.12131
zspread_bp             85.000
```

## 4. Rich or cheap: the z-spread

Give both a curve and a market price, and `fit` solves for the z-spread: the constant spread
over the zero curve at which the discounted cashflows equal the market price.

```console
$ fit bond price --maturity 2036-11-15 --coupon 4.25 --curve examples/data/usd_treasury.csv --asof 2026-11-17 --price 99.9375 | head -8
bond
====
settle             2026-11-18
clean_price         99.937500
accrued              0.035221
dirty_price         99.972721
yield_pct             4.25770
zspread_bp              0.572
```

At 99.9375 the bond trades 0.6bp cheap to the fitted curve. Against a government curve a
z-spread within a basis point or two is noise; a persistent positive spread means the bond is
cheap to its neighbours, a negative one that it is rich.

## 5. Cashflows

`fit bond cashflows` lists every remaining payment. With a curve it also shows the discount
factor (from settlement, including any z-spread) and present value of each.

```console
$ fit bond cashflows --maturity 2029-11-15 --coupon 4.25 --curve examples/data/usd_treasury.csv --asof 2026-11-17
cashflows
=========
payment     accrual_start  accrual_end    coupon  principal      amount  discount         pv
----------  -------------  -----------  --------  ---------  ----------  --------  ---------
2027-05-17  2026-11-15     2027-05-15   2.125000     0.0000    2.125000  0.979437   2.081304
2027-11-15  2027-05-15     2027-11-15   2.125000     0.0000    2.125000  0.961030   2.042188
2028-05-15  2027-11-15     2028-05-15   2.125000     0.0000    2.125000  0.943458   2.004848
2028-11-15  2028-05-15     2028-11-15   2.125000     0.0000    2.125000  0.926025   1.967803
2029-05-15  2028-11-15     2029-05-15   2.125000     0.0000    2.125000  0.908828   1.931259
2029-11-15  2029-05-15     2029-11-15   2.125000   100.0000  102.125000  0.891669  91.061656

summary
=======
settle          2026-11-18
accrued           0.035221
total_cashflow  112.750000
dirty_price     101.089057
zspread_bp           0.000
```

Note the first row: the coupon date 15 May 2027 is a Saturday, so the payment moves to
Monday the 17th. Accrual dates are never adjusted, so the coupon amount is unchanged, but the
later payment is discounted from the later date. The `pv` column sums to the dirty price.

## 6. Curve risk and key rates

Yield duration answers "what if this bond's yield moves?". A desk hedging with other
instruments needs "what if the *curve* moves, and where?". `fit bond risk` bumps the zero
curve and reprices, holding the z-spread constant.

```console
$ fit bond risk --maturity 2036-11-15 --coupon 4.25 --curve examples/data/usd_treasury.csv --asof 2026-11-17 --price 99.9375
risk
====
settle               2026-11-18
clean_price           99.937500
dirty_price           99.972721
yield_pct               4.25770
zspread_bp                0.572
modified_duration        8.0689
effective_duration       8.2366
effective_convexity     76.5961
yield_dv01             0.080667
curve_dv01             0.082343

key_rates
=========
tenor   key_dv01  pct_of_total
-----  ---------  ------------
3M     -0.000026         -0.03
6M      0.000104          0.13
1Y      0.000354          0.43
2Y      0.000785          0.95
3Y      0.001860          2.26
5Y      0.003466          4.21
7Y      0.005745          6.98
10Y     0.070000         85.01
20Y     0.000055          0.07
30Y     0.000000          0.00
total   0.082343        100.00
```

- `effective_duration` and `effective_convexity` come from a parallel shift of the zero
  curve of 1bp up and down.
- `curve_dv01` is the price change for a 1bp parallel fall in zero rates. It is slightly
  larger than `yield_dv01` because a 1bp change in a continuously compounded zero rate is a
  bigger move than 1bp in a semi-annually compounded yield (by the factor `1 + y/2`).
- `key_rates` splits `curve_dv01` across tenors. Each key rate is a triangular bump peaking
  at its own tenor and fading to zero at its neighbours, so the buckets add up to the
  parallel number (the `total` row).

Here 85% of the risk sits at the 10Y point, with the coupons spreading the rest along the
shorter tenors. To hedge this bond with 10Y and 5Y instruments, these are the amounts to
offset in each bucket.

A coarser ladder is often more useful for hedging. Pass your own tenors:

```console
$ fit bond risk --maturity 2036-11-15 --coupon 4.25 --curve examples/data/usd_treasury.csv --asof 2026-11-17 --price 99.9375 --key-tenors 2Y,5Y,10Y,30Y | tail -8
=========
tenor  key_dv01  pct_of_total
-----  --------  ------------
2Y     0.002457          2.98
5Y     0.007533          9.15
10Y    0.072326         87.83
30Y    0.000027          0.03
total  0.082343        100.00
```

## 7. Other conventions: corporates, stubs and zeros

A US corporate bond accrues on 30/360. This one was issued on 10 January 2026 with coupons
on 15 May and 15 November, so its first coupon period is short. `--issue` tells `fit` where
accrual starts:

```console
$ fit bond price --maturity 2031-11-15 --coupon 5.50 --daycount 30/360 --issue 2026-01-10 --settle 2026-03-03 --yield 6.10 | head -6
bond
====
settle             2026-03-03
clean_price         97.147566
accrued              0.809722
dirty_price         97.957288
```

```console
$ fit bond cashflows --maturity 2031-11-15 --coupon 5.50 --daycount 30/360 --issue 2026-01-10 --settle 2026-03-03 --yield 6.10 | head -5
cashflows
=========
payment     accrual_start  accrual_end    coupon  principal      amount
----------  -------------  -----------  --------  ---------  ----------
2026-05-15  2026-01-10     2026-05-15   1.909722     0.0000    1.909722
```

The first coupon is 1.9097 rather than 2.75 because it covers 125 days (30/360) from
10 January to 15 May.

A zero-coupon bond is a bond with `--coupon 0`. Its yield compounds at `--frequency`
(default 2):

```console
$ fit bond price --maturity 2036-11-15 --coupon 0 --settle 2026-11-18 --yield 4.50
bond
====
settle             2026-11-18
clean_price         64.105284
accrued              0.000000
dirty_price         64.105284
yield_pct             4.50000
macaulay_duration      9.9917
modified_duration      9.7718
convexity            100.2674
dv01                 0.062643
```

For a zero the Macaulay duration equals the time to maturity.

Supported day counts are `act/act` (ICMA), `act/act-isda`, `30/360`, `30e/360`, `act/360` and
`act/365`; frequencies are 1, 2, 4 and 12. The formulas behind every number are in
[METHODOLOGY.md](../METHODOLOGY.md).

Next: [forecasting returns](03-forecasting.md).

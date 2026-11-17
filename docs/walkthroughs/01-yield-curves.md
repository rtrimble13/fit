# Walk-through 1: Yield curves

This walk-through builds a zero curve from market quotes, checks that it reprices its inputs,
compares interpolation schemes, and reads zero, par and forward rates off the curve.

All commands run from the repository root and use the illustrative data in `examples/data`.
The numbers are not real market data. Every code block below is the tool's actual output.

- [1. The quotes file](#1-the-quotes-file)
- [2. Bootstrapping the curve](#2-bootstrapping-the-curve)
- [3. Reading rates off the curve](#3-reading-rates-off-the-curve)
- [4. Choosing an interpolation scheme](#4-choosing-an-interpolation-scheme)
- [5. Forward rates](#5-forward-rates)
- [6. The forward-implied curve](#6-the-forward-implied-curve)
- [7. A swap curve with a spot lag and a holiday calendar](#7-a-swap-curve-with-a-spot-lag-and-a-holiday-calendar)
- [8. Machine-readable output](#8-machine-readable-output)
- [9. When inputs are wrong](#9-when-inputs-are-wrong)

## 1. The quotes file

A curve is described by a CSV of market quotes. Three columns are required:

| Column | Meaning |
|--------|---------|
| `type` | `deposit`, `swap`, `par` or `zero` |
| `tenor` | a tenor from the quote's start date (`3M`, `10Y`) or an explicit maturity date |
| `rate` | the quote in percent |

Optional per-row columns `daycount`, `frequency` and `settle_days` override the defaults of
the quote type:

| Type | Instrument | Default conventions |
|------|------------|---------------------|
| `deposit` | cash deposit or bill, simple interest | ACT/360 |
| `swap` | par swap, fixed vs overnight, discounted on this curve | annual fixed leg, ACT/360 |
| `par` | coupon bond priced at 100 | semi-annual, ACT/ACT ICMA |
| `zero` | continuously compounded zero rate | ACT/365F, starts on the as-of date |

```console
$ cat examples/data/usd_treasury.csv
# Illustrative US Treasury curve as of 2026-11-17 (not real market data).
# Bills are entered as simple ACT/360 deposit-style yields; notes and bonds as par yields.
type,tenor,rate
deposit,1M,4.32
deposit,3M,4.28
deposit,6M,4.20
par,1Y,4.05
par,2Y,3.90
par,3Y,3.88
par,5Y,3.95
par,7Y,4.08
par,10Y,4.25
par,20Y,4.62
par,30Y,4.58
```

Blank lines and `#` comment lines are ignored, and column names are case-insensitive.

## 2. Bootstrapping the curve

`fit curve build` solves for the zero rate at each quote's maturity (its *pillar*) so that
every instrument reprices exactly.

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
6M     deposit  2027-05-17   0.4959     4.2000       4.2140  0.979320  0.000000
1Y     par      2027-11-17   1.0000     4.0500       4.0078  0.960714  0.000000
2Y     par      2028-11-17   2.0027     3.9000       3.8536  0.925726  0.000000
3Y     par      2029-11-19   3.0082     3.8800       3.8293  0.891192  0.000000
5Y     par      2031-11-17   5.0027     3.9500       3.9119  0.822257  0.000000
7Y     par      2033-11-17   7.0055     4.0800       4.0517  0.752886  0.000000
10Y    par      2036-11-17  10.0082     4.2500       4.2417  0.654084  0.000000
20Y    par      2046-11-19  20.0192     4.6200       4.6935  0.390786  0.000000
30Y    par      2056-11-17  30.0219     4.5800       4.5835  0.252576  0.000000
```

How to read this:

- `years` is the pillar time on the curve's ACT/365F axis; `zero_cc_pct` is the continuously
  compounded zero rate to that pillar and `discount` the discount factor.
- `error_bp` is the repricing residual of each quote: model-implied rate minus quoted rate.
  **Check this first.** A curve that does not reprice its inputs is not usable, and `fit`
  fails with an error rather than return one.
- `bootstrap_passes` is 1 because `flat-forward` is a local scheme. Splines need several
  sweeps (section 4).
- The 3Y and 20Y pillars fall on 19 November because 17 November is a weekend in those
  years and payment dates roll forward.

## 3. Reading rates off the curve

`fit curve query` interpolates at any set of tenors (`--tenors`) or dates (`--dates`).

```console
$ fit curve query --curve examples/data/usd_treasury.csv --asof 2026-11-17 --tenors 6M,1Y,2Y,4Y,5Y,10Y,15Y,30Y
curve
=====
tenor  date          years  zero_cc_pct  zero_sa_pct  par_sa_pct  fwd_inst_pct  discount
-----  ----------  -------  -----------  -----------  ----------  ------------  --------
6M     2027-05-17   0.4959       4.2140       4.2587      4.2233        3.9568  0.979320
1Y     2027-11-17   1.0000       4.0078       4.0482      4.0500        3.7524  0.960714
2Y     2028-11-17   2.0027       3.8536       3.8909      3.9000        3.7404  0.925726
4Y     2030-11-17   4.0027       3.8808       3.9187      3.9237        4.0364  0.856126
5Y     2031-11-17   5.0027       3.9119       3.9504      3.9500        4.2187  0.822257
10Y    2036-11-17  10.0082       4.2417       4.2870      4.2500        4.9150  0.654084
15Y    2041-11-17  15.0110       4.5428       4.5948      4.5005        5.1451  0.505647
30Y    2056-11-17  30.0219       4.5835       4.6364      4.5800        4.3633  0.252576
```

| Column | Meaning |
|--------|---------|
| `zero_cc_pct` | continuously compounded zero rate |
| `zero_sa_pct` | the same zero rate with semi-annual compounding |
| `par_sa_pct` | coupon of a semi-annual bond from the as-of date to this date that prices at 100 |
| `fwd_inst_pct` | instantaneous forward rate at this date |
| `discount` | discount factor |

`par_sa_pct` returns the input quote at every `par` pillar (2Y is 3.9000, 10Y is 4.2500), which
is the repricing property seen from the other side. The 4Y and 15Y points are interpolated.

Explicit dates work the same way:

```console
$ fit curve query --curve examples/data/usd_treasury.csv --asof 2026-11-17 --dates 2027-03-31,2030-06-30,2041-12-31
curve
=====
tenor       date          years  zero_cc_pct  zero_sa_pct  par_sa_pct  fwd_inst_pct  discount
----------  ----------  -------  -----------  -----------  ----------  ------------  --------
2027-03-31  2027-03-31   0.3671       4.2509       4.2964      4.2725        4.1087  0.984515
2030-06-30  2030-06-30   3.6192       3.8643       3.9019      3.9046        4.0364  0.869484
2041-12-31  2041-12-31  15.1315       4.5476       4.5997      4.5038        5.1451  0.502520
```

## 4. Choosing an interpolation scheme

Five schemes are available through `--interp`:

| Scheme | Interpolates | Forwards | Use when |
|--------|--------------|----------|----------|
| `flat-forward` (default) | `ln DF` linearly | piecewise constant, jump at pillars | robust default; pricing and risk |
| `linear-zero` | zero rates linearly | discontinuous, can saw-tooth | matching systems that use it |
| `cubic-zero` | zero rates, natural cubic spline | smooth; can oscillate | smooth forwards matter and inputs are clean |
| `monotone-cubic` | `-ln DF`, monotone cubic | smooth and well behaved | smooth discount factors without overshoot |
| `monotone-convex` | the forward curve itself (Hagan-West) | continuous; positive where the discrete forwards are; no overshoot where they are monotone | forwards between pillars matter |

All five reprice the inputs exactly. They differ *between* pillars, and the difference shows up
most clearly in forwards. Here is the instantaneous forward at a few off-pillar points:

```console
$ for m in flat-forward linear-zero cubic-zero monotone-cubic monotone-convex; do echo "--- $m"; fit curve query --curve examples/data/usd_treasury.csv --asof 2026-11-17 --interp $m --tenors 4Y,8Y,15Y,25Y --format csv | cut -d, -f1,4,7; done
--- flat-forward
tenor,zero_cc_pct,fwd_inst_pct
4Y,3.8807856996,4.03640334226
8Y,4.13081838154,4.68497034627
15Y,4.54279215034,5.1451224303
25Y,4.62751791689,4.36328075924
--- linear-zero
tenor,zero_cc_pct,fwd_inst_pct
4Y,3.87047236566,4.03717944126
8Y,4.11600130651,4.62520826286
15Y,4.47796154106,5.18180408944
25Y,4.65295710856,4.35343268326
--- cubic-zero
tenor,zero_cc_pct,fwd_inst_pct
4Y,3.85557570972,4.03749156661
8Y,4.11977072661,4.635834874
15Y,4.52690762201,5.2645857337
25Y,4.68987497829,4.27752141067
--- monotone-cubic
tenor,zero_cc_pct,fwd_inst_pct
4Y,3.86135847466,4.03101400017
8Y,4.11874706776,4.62995651277
15Y,4.55593676975,5.31914474415
25Y,4.65935270063,4.34364901846
--- monotone-convex
tenor,zero_cc_pct,fwd_inst_pct
4Y,3.8594292572,4.03535990748
8Y,4.11982892323,4.65259121019
15Y,4.54315997896,5.33306533511
25Y,4.65461002465,4.29028180642
```

Zero rates agree to within a few basis points; forwards can differ by tens of basis points,
especially in the wide 10Y to 30Y gaps. If a trade is sensitive to forwards between pillars,
the interpolation choice is a pricing decision, not a technicality.

The spline is a global scheme: each pillar depends on all the others, so the bootstrap sweeps
repeatedly until everything reprices.

```console
$ fit curve build --curve examples/data/usd_treasury.csv --asof 2026-11-17 --interp cubic-zero | head -9
curve
=====
as_of                   2026-11-17
interpolation           cubic-zero
extrapolation            flat-zero
calendar                  weekends
nodes                           11
bootstrap_passes                 5
max_repricing_error_bp  0.00000000
```

`monotone-convex` constructs the forward curve directly, following Hagan and West. Between two
pillars the instantaneous forward averages exactly to the discrete forward. The forward is
continuous at the pillars, where `flat-forward` forwards jump. Where the discrete forwards are
positive it stays positive, and where they rise or fall steadily it does not overshoot. The
scheme is local, since a quote affects forwards only on neighbouring intervals, but it is not
strictly local, so the bootstrap again takes a few sweeps:

```console
$ fit curve build --curve examples/data/usd_treasury.csv --asof 2026-11-17 --interp monotone-convex | head -9
curve
=====
as_of                        2026-11-17
interpolation           monotone-convex
extrapolation              flat-forward
calendar                       weekends
nodes                                11
bootstrap_passes                      4
max_repricing_error_bp       0.00000000
```

The default remains `flat-forward`. The method and its validation are described in
[METHODOLOGY.md](../METHODOLOGY.md#3-yield-curves) and
[ADR 0014](../adr/0014-monotone-convex-interpolation.md).

### Beyond the last pillar

Past the last quote, here the 30Y bond, the curve has to be extrapolated. `--extrapolation`
picks the rule. `flat-zero` holds the last zero rate. `flat-forward` holds the instantaneous
forward at the last pillar, so forwards stay continuous; QuantLib's curves use the same rule.
Without the option, each scheme keeps its own rule: `flat-zero` for `linear-zero` and
`cubic-zero`, and `flat-forward` for the others. `fit curve build` reports the rule in use.

```console
$ for e in flat-zero flat-forward; do echo "--- $e"; fit curve query --curve examples/data/usd_treasury.csv --asof 2026-11-17 --interp linear-zero --extrapolation $e --tenors 25Y,40Y,50Y --format csv | cut -d, -f1,4,7,8; done
--- flat-zero
tenor,zero_cc_pct,fwd_inst_pct,discount
25Y,4.65295710856,4.35343268326,0.312233445935
40Y,4.59302598108,4.59302598108,0.159060800669
50Y,4.59302598108,4.59302598109,0.100444608483
--- flat-forward
tenor,zero_cc_pct,fwd_inst_pct,discount
25Y,4.65295710856,4.35343268326,0.312233445935
40Y,4.50317439458,4.2335704283,0.16488557933
50Y,4.4492476964,4.23357042828,0.107936896773
```

The 25Y point lies inside the curve, so it does not depend on the rule. At 50 years the
discount factors differ by 7.5% and the zero rates by 14bp. The choice reaches every cash
flow past the last pillar, so it moves the price of a 40-year bond by about three quarters of a
point:

```console
$ for e in flat-zero flat-forward; do echo "--- $e"; fit bond price --maturity 2066-11-15 --coupon 4.5 --curve examples/data/usd_treasury.csv --asof 2026-11-17 --interp linear-zero --extrapolation $e --format csv | grep -E '^(clean_price|zspread_bp),'; done
--- flat-zero
clean_price,98.3954340814
zspread_bp,0
--- flat-forward
clean_price,99.13682833
zspread_bp,0
```

Bonds that mature inside the curve, including their [horizon analysis](03-forecasting.md), do
not depend on the rule.

## 5. Forward rates

`fit curve forwards` tabulates consecutive forward periods of a given length.

```console
$ fit curve forwards --curve examples/data/usd_treasury.csv --asof 2026-11-17 --interp monotone-cubic --tenor 6M --end 4Y
forwards
========
start       end         start_years  fwd_simple_pct  fwd_cc_pct  zero_cc_pct  discount
----------  ----------  -----------  --------------  ----------  -----------  --------
2026-11-17  2027-05-17       0.0000          4.2000      4.2140       4.2140  0.979320
2027-05-17  2027-11-17       0.4959          3.7891      3.8050       4.0078  0.960714
2027-11-17  2028-05-17       1.0000          3.6874      3.7042       3.9068  0.943133
2028-05-17  2028-11-17       1.4986          3.6788      3.6952       3.8535  0.925727
2028-11-17  2029-05-17       2.0027          3.7265      3.7433       3.8317  0.908701
2029-05-17  2029-11-17       2.4986          3.8020      3.8178       3.8293  0.891380
2029-11-17  2030-05-17       3.0027          3.9064      3.9222       3.8425  0.874210
2030-05-17  2030-11-17       3.4986          3.9774      3.9922       3.8614  0.856792
```

- `fwd_simple_pct` is the simply compounded forward rate over the period in the chosen
  `--daycount` (default ACT/360), the rate a forward-starting deposit would carry.
- `fwd_cc_pct` is the same forward, continuously compounded on ACT/365F.
- The first row is spot-starting, so it reproduces the 6M deposit quote (4.2000).

The curve is inverted out to three years, so forwards sit below spot rates there and then rise.

## 6. The forward-implied curve

Adding `--horizon` to `curve query` reports the curve the market implies for a future date,
`DF_h(T) = DF(T) / DF(h)`. This is the "forwards are realised" curve used by
[horizon analysis](03-forecasting.md).

```console
$ fit curve query --curve examples/data/usd_treasury.csv --asof 2026-11-17 --tenors 1Y,2Y,5Y,10Y --horizon 1Y
forward_curve
=============
tenor  date          years  zero_cc_pct  zero_sa_pct  par_sa_pct  fwd_inst_pct  discount
-----  ----------  -------  -----------  -----------  ----------  ------------  --------
1Y     2028-11-17   1.0027       3.6997       3.7342      3.7443        3.7404  0.963581
2Y     2029-11-17   2.0027       3.7404       3.7755      3.7901        3.7811  0.927827
5Y     2032-11-17   5.0055       3.9907       4.0308      4.0217        4.4010  0.818932
10Y    2037-11-17  10.0082       4.3553       4.4031      4.3525        5.1451  0.646687
```

Compare with section 3: the market prices the 2Y par rate one year from now at about
3.79 against 3.90 today, and the 10Y at about 4.35 against 4.25. A view on rates is a view
relative to these forwards, not relative to today's spot curve.

## 7. A swap curve with a spot lag and a holiday calendar

Swap and deposit quotes normally start two business days after the trade date. `--spot-lag`
sets that lag and `--calendar us` applies US government bond market holidays.

```console
$ fit curve build --curve examples/data/usd_sofr.csv --asof 2026-11-17 --spot-lag 2 --calendar us
curve
=====
as_of                     2026-11-17
interpolation           flat-forward
extrapolation           flat-forward
calendar                          us
nodes                             13
bootstrap_passes                   1
max_repricing_error_bp    0.00000000

nodes
=====
tenor  type     pillar        years  quote_pct  zero_cc_pct  discount  error_bp
-----  -------  ----------  -------  ---------  -----------  --------  --------
1W     deposit  2026-11-27   0.0274     4.3300       4.3880  0.998799  0.000000
1M     deposit  2026-12-21   0.0932     4.3100       4.3631  0.995944  0.000000
3M     deposit  2027-02-19   0.2575     4.2400       4.2782  0.989043  0.000000
6M     deposit  2027-05-19   0.5014     4.1200       4.1373  0.979470  0.000000
1Y     swap     2027-11-19   1.0055     3.9600       3.9390  0.961169  0.000000
2Y     swap     2028-11-20   2.0110     3.7400       3.7194  0.927933  0.000000
3Y     swap     2029-11-19   3.0082     3.6800       3.6596  0.895755  0.000000
5Y     swap     2031-11-19   5.0082     3.7100       3.6920  0.831185  0.000000
7Y     swap     2033-11-21   7.0164     3.8000       3.7888  0.766561  0.000000
10Y    swap     2036-11-19  10.0137     3.9300       3.9329  0.674470  0.000000
15Y    swap     2041-11-19  15.0164     4.0800       4.1070  0.539705  0.000000
20Y    swap     2046-11-19  20.0192     4.1200       4.1488  0.435808  0.000000
30Y    swap     2056-11-20  30.0301     4.0200       3.9752  0.303079  0.000000
```

The start date is Thursday 19 November. The 1W deposit would mature on Thursday 26 November,
which is Thanksgiving, so its pillar rolls to the 27th. Swap periods accrue between adjusted
dates, as swaps do: the 2Y swap's second period ends on Sunday 19 November 2028, so it runs to
Monday the 20th and accrues for 367 days. Calendars available are `none`,
`weekends` (default) and `us`; `--holidays FILE` adds dates of your own (one ISO date per
line) to any of them.

## 8. Machine-readable output

Every command accepts `--format csv` or `--format json`. Text tables are rounded for reading;
CSV and JSON carry full precision.

```console
$ fit curve query --curve examples/data/usd_treasury.csv --asof 2026-11-17 --tenors 2Y,10Y --format json
{
  "curve": [
    {"tenor": "2Y", "date": "2028-11-17", "years": 2.00273972603, "zero_cc_pct": 3.85356091103, "zero_sa_pct": 3.89092533123, "par_sa_pct": 3.9, "fwd_inst_pct": 3.74040706016, "discount": 0.925726175164},
    {"tenor": "10Y", "date": "2036-11-17", "years": 10.0082191781, "zero_cc_pct": 4.2417094536, "zero_sa_pct": 4.28700938309, "par_sa_pct": 4.25, "fwd_inst_pct": 4.9150463883, "discount": 0.654083945313}
  ]
}
```

```console
$ fit curve query --curve examples/data/usd_treasury.csv --asof 2026-11-17 --tenors 2Y,10Y --format json | jq '.curve[] | {tenor, zero_cc_pct}' -c
{"tenor":"2Y","zero_cc_pct":3.85356091103}
{"tenor":"10Y","zero_cc_pct":4.2417094536}
```

`--precision N` changes the decimal places of the text table only:

```console
$ fit curve query --curve examples/data/usd_treasury.csv --asof 2026-11-17 --tenors 2Y,10Y --precision 8
curve
=====
tenor  date              years  zero_cc_pct  zero_sa_pct  par_sa_pct  fwd_inst_pct    discount
-----  ----------  -----------  -----------  -----------  ----------  ------------  ----------
2Y     2028-11-17   2.00273973   3.85356091   3.89092533  3.90000000    3.74040706  0.92572618
10Y    2036-11-17  10.00821918   4.24170945   4.28700938  4.25000000    4.91504639  0.65408395
```

## 9. When inputs are wrong

Errors go to standard error, name the file and line, and leave standard output empty. Data
errors exit with code 1 and command-line mistakes with code 2, so scripts can tell them apart.

```console
$ printf 'type,tenor,rate\npar,2Y,3.90\nfuture,5Y,4.00\n' > /tmp/bad.csv; fit curve build --curve /tmp/bad.csv --asof 2026-11-17
fit: error: /tmp/bad.csv:3: unknown quote type 'future' (expected deposit, swap, par or zero)
```

(exit code 1)

```console
$ printf 'type,tenor,rate\npar,2Y,3.90\npar,24M,3.95\n' > /tmp/bad.csv; fit curve build --curve /tmp/bad.csv --asof 2026-11-17
fit: error: quotes '2Y' and '24M' mature on the same date 2028-11-17
```

(exit code 1)

```console
$ fit curve build --curve examples/data/usd_treasury.csv
fit: error: missing required option '--asof'
```

(exit code 2)

Next: [pricing a bond](02-bond-pricing.md).

# Walk-throughs

Practical, end-to-end examples of every main feature. Each one uses the illustrative data in
[`examples/data`](../../examples/data) and shows the tool's actual output, so you can run the
commands from the repository root and compare.

| # | Walk-through | Commands covered | You will learn to |
|---|--------------|------------------|-------------------|
| 1 | [Yield curves](01-yield-curves.md) | `fit curve build`, `query`, `forwards` | bootstrap a curve from quotes, verify repricing, choose an interpolation scheme and extrapolation, read zero, par and forward rates, derive the forward-implied curve, build a projection curve on an OIS discount curve |
| 2 | [Bond pricing and risk](02-bond-pricing.md) | `fit bond price`, `yield`, `cashflows`, `risk` | move between price and yield, price off a curve, measure richness with z-spread, list cashflows, compute duration, convexity, DV01 and key-rate risk |
| 3 | [Forecasting](03-forecasting.md) | `fit bond forecast` | project returns to a horizon, attribute them to carry, roll-down and rate moves, run rate and curve scenarios, find breakevens |
| 4 | [Portfolio valuation and risk](04-portfolio.md) | `fit portfolio value`, `risk`, `scenario`, `cashflows` | mark a book to market and model, aggregate risk, build a key-rate ladder, stress the book, project cashflows, export reports |
| 5 | [Pre-trade analysis](05-pretrade.md) | `fit pretrade`, `fit bond price --face` | cost a ticket, see a trade's effect on portfolio risk, check a DV01 limit, size a duration-neutral switch, stress the post-trade book |

They are written to be read in order, but each stands alone.

## Before you start

Build the tool and put it on your `PATH` (or replace `fit` below with `build/release/fit`):

```console
$ cmake --preset release
$ cmake --build --preset release
$ export PATH="$PWD/build/release:$PATH"
$ fit --version
fit 0.4.0
```

Conventions used everywhere:

- rates, coupons and yields are in **percent**; spreads and shifts in **basis points**;
- prices are per **100 face**; position sizes are face amounts;
- dates are `YYYY-MM-DD`; tenors look like `1W`, `3M`, `10Y`;
- `fit <command> --help` lists every option with its default.

The example market data and positions are invented for illustration. They are internally
consistent but are not real quotes.

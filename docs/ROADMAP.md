# Development Roadmap

This roadmap describes what `fit` delivers today, what it deliberately does not, and the
sequence in which capability will be added. It is organised by milestone rather than by date,
and a milestone is reached when its exit criteria are met. Versions are separate from
milestones. A new version is released on every merge that changes what fit ships
([ADR 0018](adr/0018-continuous-versioning-and-automated-releases.md),
[versioning and releases](versioning-and-releases.md)), so one milestone spans many versions.
The exception is version 1.0.0, which is released when the stability commitment is met.
Priorities are reviewed at each milestone, and changes are made by pull request to this file.

Before ADR 0018 the milestones were labelled v0.1 to v0.5. Older documents, including accepted
ADRs, use those labels: v0.1 is M1, v0.2 is M2, v0.3 is M3, v0.4 is M4 and v0.5 is M5.

Methodology decisions referenced here are recorded in the [ADRs](adr/README.md).

## Guiding principles

1. **Correct before broad.** A feature ships only with independent numerical validation
   ([ADR 0012](adr/0012-testing-and-numerical-validation.md)). Fewer instruments priced right
   beat many priced approximately.
2. **Explicit conventions.** Every number can be traced to stated conventions and methodology.
3. **Composable.** Plain files in, plain tables out; the library stays usable without the CLI.
4. **Small surface.** No dependency, option or abstraction is added without a concrete user.

## Status summary

| Milestone | Theme | Status |
|-----------|-------|--------|
| M1 | MVP: curves, fixed-rate bonds, horizon analysis, portfolio and pre-trade | Delivered (0.1.0) |
| M2 | Curve depth | In progress (0.2.0 delivers part of it) |
| M3 | Instrument coverage | Planned |
| M4 | Portfolio and risk depth | Planned |
| M5 | Integration and performance | Planned |
| 1.0 | Stability commitment | Planned: released as version 1.0.0 |

---

## M1 — MVP (delivered in 0.1.0)

### Scope

**Foundations**
- Dates, tenors, business-day conventions and schedule generation.
- Calendars: weekends-only, rule-based US government bond calendar, custom holiday file.
- Day counts: ACT/360, ACT/365F, 30/360 US, 30E/360, ACT/ACT ISDA, ACT/ACT ICMA.

**Curves** — `fit curve build | query | forwards`
- Single-curve iterative bootstrap from `deposit`, `swap` (OIS-style, annual fixed), `par` and
  `zero` quotes, with repricing residuals reported.
- Interpolation: `flat-forward` (default), `linear-zero`, `cubic-zero`, `monotone-cubic`.
- Queries for discount factors, zero rates and forward rates.

**Bonds** — `fit bond price | yield | cashflows | risk | forecast`
- Fixed-rate bullet bonds and zero-coupon bonds.
- Clean and dirty price, accrued interest, street-convention yield to maturity.
- Macaulay and modified duration, convexity, yield DV01.
- Z-spread (continuously compounded) to the zero curve.
- Effective duration and convexity, curve DV01 and key-rate DV01s by bump-and-reprice.
- Horizon analysis under `unchanged` and `forward` curve scenarios with parallel shifts,
  constant z-spread, and an exact P&L decomposition.

**Portfolio and pre-trade** — `fit portfolio value | risk | scenario | cashflows`, `fit pretrade`
- Positions from CSV; mark to supplied market price, otherwise to model.
- Aggregated market value, DV01, duration, key-rate ladder, cash-flow ladder, scenario P&L.
- Pre-trade comparison of portfolio risk before and after proposed trades, settlement amounts,
  optional DV01 limit check.

**Engineering**
- `fit_core` static library plus thin CLI; no third-party runtime dependencies.
- Table, CSV and JSON output; defined exit codes and stream discipline.
- Unit and CLI integration tests, sanitizer builds, CI on GCC and Clang.
- README, methodology document, walk-throughs, ADRs, contributor and PR standards.

### Known limitations

These are limitations of the current version, not defects. Each is addressed by a later
roadmap item where noted.

| Area | Limitation | Addressed in |
|------|------------|--------------|
| Curve construction | Projection curves are built in sequence on their discount curve, not jointly; no instruments that tie curves together (basis swaps) | M2 |
| Curve construction | No basis swaps; no turn-of-year or meeting-date handling; futures convexity adjustments are inputs, not modelled | M2 |
| Curve construction | Swap legs have no payment lag, and floating coupons are par coupons (no indexed coupons or spreads) | M2 |
| Curve construction | Exact bootstrap only; no smoothing or parametric fit | M2 |
| Interpolation | The default, `flat-forward`, has forwards that are discontinuous at nodes, and `cubic-zero` can oscillate; `monotone-convex` avoids both but is not yet the default | M2 |
| Extrapolation | Beyond the last pillar the zero rate or the last forward is held flat, selectable with `--extrapolation`; no ultimate-forward-rate method | Not scheduled |
| Calendars | Only weekends-only, a rule-based US government bond calendar and user-supplied holiday files; the rule-based calendar knows only a handful of historical ad hoc market closures | M2 |
| Instruments | Fixed-rate bullets and zeros only | M3 |
| Yield conventions | Street convention only; no US Treasury final-period simple yield, no money-market or discount yields for bills | M3 |
| Settlement | No ex-dividend periods; no odd long first/last coupon handling beyond a short front stub | M3 |
| Spreads | Z-spread only; no G-spread, I-spread, asset-swap spread or OAS | M3 |
| Risk | Key-rate risk is to zero-rate bumps, not to par market quotes; no spread (CS01) or basis risk | M4 |
| Scenarios | Parallel shifts and piecewise-linear twists typed on the command line; no named or file-based scenario sets | M4 |
| Horizon analysis | No financing leg (repo); deterministic, constant-spread only | M4 |
| Portfolio | Portfolio and pre-trade valuation use one settlement date for every position (default: the as-of date) rather than per-instrument settlement lags | M4 |
| Portfolio | Single currency; no grouping/drill-down hierarchy; no benchmark-relative measures | M4 |
| Data | File inputs only; no market-data or position-system connectivity | M5 |
| Performance | Single-threaded; bump-and-reprice risk | M5 |
| Platforms | Linux with GCC 13+ or Clang 17+ only | Cross-cutting |

### Non-goals

The following are outside the intended scope of `fit` and are not planned:

- **System of record.** `fit` does not book trades, keep positions, or produce official
  settlement amounts or accounting valuations.
- **Order or execution management.** No connectivity to trading venues.
- **Derivatives pricing library.** No options, swaptions, caps/floors or stochastic rate
  models beyond what bond optionality requires in M3.
- **Credit modelling.** No default, recovery or rating-migration models; credit is represented
  as a spread.
- **Real-time streaming analytics.** The tool is request/response; it is not a tick-driven
  pricing engine.
- **Graphical user interface.**
- **Regulatory capital and reporting calculations.**

---

## M2 — Curve depth

**Goal:** curves a rates desk would accept for marking and for forward-rate analysis.

| Item | Description |
|------|-------------|
| Monotone convex interpolation | Hagan–West monotone convex scheme, with continuous positive forwards; candidate new default after validation (requires an ADR superseding the default in ADR 0007). *Scheme available as `monotone-convex` ([ADR 0014](adr/0014-monotone-convex-interpolation.md)); the default decision is outstanding.* |
| Parametric fitting | Nelson–Siegel and Nelson–Siegel–Svensson least-squares fit to bond prices or yields, with fit diagnostics (per-instrument residuals, RMSE), as a complement to exact bootstrap |
| Multi-curve construction | Separate discount (OIS) and projection curves; dual-curve bootstrap of term-rate swaps; curve sets addressable by name. *Delivered: dual-curve bootstrap and `irs` quotes ([ADR 0016](adr/0016-multi-curve-construction.md)), curve sets and `--curve-name` ([ADR 0017](adr/0017-curve-sets.md)).* |
| Additional instruments for construction | Interest-rate futures with convexity adjustment, FRAs, swaps with explicit fixed/floating leg conventions, basis swaps. *Delivered: FRAs and quarterly IMM futures with an input convexity adjustment (0.3.0). Open: basis swaps, payment lags and further leg conventions, a convexity model.* |
| Turn-of-year and jumps | Explicit jump pillars; step-function overlay for central-bank meeting dates |
| Extrapolation choices | Flat forward and flat zero extrapolation, selectable and documented. *Delivered: `--extrapolation` ([ADR 0015](adr/0015-selectable-extrapolation.md)).* |
| Calendars | TARGET, UK, Japan and joint calendars; data-driven holiday tables with effective dates; calendar validation against published schedules |
| Curve analytics | Par curve and forward curve output grids, curve differencing between two dates or two builds, rich/cheap residuals of bonds against a fitted curve |
| Curve persistence | Save a built curve and reload it without re-bootstrapping |

**Exit criteria**
- Monotone convex and NSS validated against independent implementations on a published or
  reconstructed data set, with results recorded in tests.
- Dual-curve bootstrap reprices all inputs within tolerance on both curves.
- Each added calendar matches the published holiday schedule over a tested range of years.

## M3 — Instrument coverage

**Goal:** cover the instruments held in a typical government/credit bond book.

| Item | Description |
|------|-------------|
| Bills and money-market conventions | Discount yield, money-market yield, bond-equivalent yield; US Treasury convention for the final coupon period; simple-yield conventions |
| Odd coupons and ex-dividend | Long and short first/last coupons; ex-dividend periods and negative accrued |
| Floating-rate notes | Compounded-in-arrears overnight and term-rate FRNs, discount margin, spread duration; depends on the projection curves of M2, delivered in 0.2.0 |
| Amortising and sinking-fund bonds | Principal schedules, weighted-average life, factor handling |
| Callable and puttable bonds | Yield to call/put/worst; option-adjusted spread and effective duration on a short-rate lattice; requires a volatility input and an ADR for the model choice |
| Inflation-linked bonds | Index ratio with lag and interpolation, real yield, inflation-adjusted accrued and settlement amount |
| Additional spread measures | G-spread, I-spread, par asset-swap spread |
| Instrument definition files | Bond terms loaded from a reference-data CSV keyed by identifier, shared by bond, portfolio and pre-trade commands |

**Exit criteria**
- Each instrument type validated against at least one independent source across a set of
  cases including stubs, month-ends and leap years.
- Yield conventions selectable explicitly; defaults unchanged for existing users unless an ADR
  says otherwise.

## M4 — Portfolio and risk depth

**Goal:** risk a desk can hedge from and performance it can explain.

| Item | Description |
|------|-------------|
| Par-point key-rate risk | Bump market quotes and re-bootstrap, giving sensitivities to the curve's own instruments and implied hedge notionals; offered alongside zero-rate bumps (extends ADR 0009) |
| Spread risk | CS01 / spread duration, by issuer, sector or rating bucket |
| Scenario library | Named scenario sets in files: parallel, steepener/flattener, twist, butterfly, key-rate vectors, user-defined curves; applicable to instant and horizon analysis |
| Historical VaR | Historical-simulation VaR and expected shortfall from a supplied history of curve moves; sensitivity-based and full-revaluation modes |
| P&L attribution | Day-over-day explain between two valuation dates: carry, roll-down, curve (by key rate), spread, trading, residual |
| Financing and net carry | Repo rate inputs; net carry and forward price; breakeven yield and spread changes |
| Benchmark-relative analysis | Active duration, active key-rate exposure and active weights against a benchmark portfolio |
| Grouping and limits | Aggregation by arbitrary position attributes (book, sector, issuer, maturity bucket); limit sets defined in a file and checked in `portfolio` and `pretrade` |
| Multi-currency | Per-currency curves, FX conversion to a reporting currency |
| Hedge suggestion | Minimum-instrument hedge of a key-rate ladder from a user-supplied hedge universe |

**Exit criteria**
- Par-point key-rate risk sums to parallel quote-shift risk within stated tolerance.
- Attribution components sum to actual P&L with a reported, bounded residual.
- VaR reproduces a hand-checkable reference case.

## M5 — Integration and performance

**Goal:** fit into desk infrastructure and scale to large books.

| Item | Description |
|------|-------------|
| Market-data adapters | Pluggable sources for curve quotes and prices behind the existing tabular interface; first adapters for vendor file drops and a generic HTTP/CSV source. Any adapter requiring a third-party library needs an ADR (ADR 0003) |
| Configuration profiles | Named profiles for conventions, curve definitions, key-tenor ladders and limits, selectable per invocation; no hidden global state |
| Python bindings | A Python package over `fit_core` exposing curves, bonds, analytics and portfolio valuation with the same numbers as the CLI |
| Batch and server mode | Process a file of requests in one invocation with shared curve builds; optional long-running local service mode for interactive front ends |
| Parallelism | Multi-threaded portfolio valuation and scenario runs with deterministic, order-independent aggregation |
| Algorithmic differentiation | Adjoint sensitivities for curve risk, replacing bump-and-reprice where instrument support allows; validated against bumps |
| Structured logging and audit | Optional run manifest recording inputs, conventions, versions and checksums for reproducibility |

**Exit criteria**
- Python and CLI results agree to within floating-point noise on the validation corpus.
- Parallel runs produce results identical to serial runs.
- Published benchmark numbers for representative portfolio sizes.

## 1.0 — Stability commitment (version 1.0.0)

- CLI contract (commands, options, input schemas, output field names, exit codes) frozen under
  semantic versioning; breaking changes only in major releases.
- `fit_core` public API reviewed, documented and versioned.
- Methodology document complete for every calculation, suitable for model validation.
- Validation corpus (below) passing in CI on all supported platforms.

---

## Cross-cutting workstreams

These run alongside the milestones.

### Validation corpus
- A versioned set of instruments, curves and expected results from independent sources,
  covering every convention combination supported, including stubs, month-ends, leap years and
  negative rates.
- Extended with each milestone; a gate for reaching M2 and every later milestone.

### Benchmarks
- Repeatable micro- and macro-benchmarks: curve build, single-bond analytics, portfolio
  valuation and risk at several book sizes.
- Tracked over time to detect performance regressions.

### Quality and tooling
- `clang-tidy` enforced in CI once the baseline is clean.
- Coverage reporting (informational).
- Fuzzing of the CSV reader, argument parser and date/tenor parsers.
- Property-based tests for curve and schedule invariants.

### Packaging and platforms
- Versioned release binaries with checksums. *Delivered: a Linux x86-64 archive and its SHA-256
  checksum with every release ([ADR 0018](adr/0018-continuous-versioning-and-automated-releases.md)).*
  Still open: bit-for-bit reproducible release builds.
- `cmake --install` support and a CMake package config so that `fit_core` can be consumed with
  `find_package`.
- macOS and Windows builds added to CI when there is a committed user.
- Shell completion scripts and a man page.

### Documentation
- Walk-through for every new command, with runnable example data.
- Methodology section for every new calculation, merged with the feature.
- Generated API reference for `fit_core`.

## How to influence the roadmap

Open a feature request describing the desk workflow and the numbers needed, not just the
feature. Items are prioritised by (1) correctness risk in existing functionality, (2) breadth of
desk workflows unblocked, and (3) implementation and validation cost. Changes to this document
are made by pull request.

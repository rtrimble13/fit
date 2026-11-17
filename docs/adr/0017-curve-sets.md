# 0017. Curve sets addressable by name

- **Status:** Proposed
- **Date:** 2026-11-17

## Context

[ADR 0016](0016-multi-curve-construction.md) builds a projection curve on a discount curve from
an explicit pair of options. That covers one discount curve and one projection curve under
shared conventions. A desk's market is often bigger: an OIS curve, several term-rate curves on
it, and independent curves such as a government bond curve. Each can have its own
interpolation, calendar or spot lag. Commands then need a way to say which of these curves they
mean. The roadmap asks for curve sets addressable by name, and the CLI should keep the pair as a
shorthand.

## Decision

**Curve-set file** (`--curve-set FILE`), a CSV file ([ADR 0010](0010-csv-inputs-and-table-csv-json-outputs.md))
with one row per curve:

| Column | Required | Meaning |
|--------|----------|---------|
| `name` | yes | the curve's name; unique and non-empty |
| `quotes` | yes | its quotes file, relative to the set file's directory unless absolute |
| `discount` | no | the name of the curve that discounts it; empty, or its own name, for a curve that discounts itself |
| `interp`, `extrapolation`, `spot_lag` | no | override the command-line value for this curve |
| `calendar` | no | `none`, `weekends` or `us`: replaces the command's calendar, and its `--holidays`, for this curve |

Rows may be listed in any order. Each curve is built after the curve that discounts it,
otherwise in file order. An unknown discount name, a duplicate name, or curves that discount
each other in a cycle are errors that name the curves. Errors from a curve's bootstrap are
prefixed `curve '<name>':` whenever a market has more than one curve. Errors in a quotes file
give that file and line, preceded by the set file's row when the curve comes from a set.

**One model for every source.** All three sources produce a set of named curves:

| Source | Curves |
|--------|--------|
| `--curve FILE` | `curve` |
| `--discount-curve FILE --projection-curve FILE` | `discount`, and `projection` discounted on it |
| `--curve-set FILE` | as the file names them |

The three are mutually exclusive.

**Selection.** Every command except `fit curve build` takes `--curve-name NAME`, which selects
the curve it works with:

- `fit curve query` and `fit curve forwards` report the selected curve.
- Bond, portfolio and pre-trade commands discount on the selected curve's discount curve: on
  the curve itself when it discounts itself, otherwise on the curve it was built on.
- Defaults: `curve` for `--curve`, `projection` for the pair, and a set's only curve. A set with
  more than one curve requires `--curve-name`; leaving it out is a usage error (exit 2) that
  lists the names.
- An unknown name is an error (exit 1) that lists the names, as an unknown `--interp` is.

**Build output.** `fit curve build` builds and reports every curve and does not take
`--curve-name`. For a pair or a set it reports:

- a `curves` table with one row per curve: `curve`, `discount_curve`, `interpolation`,
  `extrapolation`, `calendar`, `nodes`, `bootstrap_passes`, `max_repricing_error_bp`;
- a `market` table with the as-of date and the command's calendar;
- one `nodes` table led by a `curve` column.

`--curve` keeps its single-curve output.

**Library.** Building a set (`build_curve_set`) and reading a set file (`load_curve_set`) live
in the `curve` module, beside `load_quotes`. Ordering curves by their discount dependencies is
curve logic, not CLI logic.

## Consequences

- Scripts address curves by stable names. A typo is an error rather than a silent default.
- One market can mix independent curves with projection curves, each with its own conventions,
  and every command can price off any of them.
- The pair and `--curve` are special cases of a set. They share the same building code and
  error messages, so behaviour cannot drift between them.
- A projection curve may itself discount another curve (a chain), which the ordering handles.
  Whether that is meaningful is left to the user.
- `--holidays` cannot be set per curve: a row's `calendar` starts from the named calendar alone.
  This can be added when a user needs it.
- Every command builds every curve in the set, including curves it does not use. For desk-sized
  sets the cost is negligible ([ADR 0008](0008-iterative-bootstrap.md)).

## Alternatives considered

- **JSON or YAML set files.** They allow nesting, but need a new parser or dependency
  ([ADR 0003](0003-zero-runtime-dependencies.md)), and CSV is the established input format.
  Rejected.
- **Repeated `--curve NAME=FILE` options.** Command lines become long, and per-curve conventions
  get awkward. Rejected.
- **An implicit default curve for sets**, such as the first or last row. It is hidden and
  depends on file order. Rejected: with several curves the name is required.
- **Separate selectors for discounting and for reporting**, such as `--discount-name`. That
  means two rules where one suffices: a bond is discounted on the discount curve of whichever
  curve it is priced against. Rejected until floating-rate instruments need both a projection
  and a discount curve at once.

# 0005. Units and conventions at the CLI boundary

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

Unit mistakes are the most common source of wrong numbers in fixed-income tooling: a coupon of
`4.25` read as 425%, a shift of `25` read as 25% rather than 25 basis points, a price per 1 face
read as per 100. Desk users think and speak in percent, basis points and prices per 100;
numerical code is cleanest in decimals and year fractions. Mixing the two inside the library
guarantees errors.

## Decision

There is exactly one place where units are converted: the CLI boundary (argument parsing, input
file loading and output rendering).

**At the boundary (inputs and outputs):**

| Quantity | Unit | Example |
|----------|------|---------|
| Rates, coupons, yields | percent per annum | `4.25` means 4.25% |
| Spreads, curve shifts, bumps | basis points | `25` means 0.25% |
| Prices | per 100 face | `98.50` |
| Face / notional | currency units | `10000000` |
| Monetary risk (DV01, key-rate DV01, P&L) | currency units | |
| Dates | ISO 8601 `YYYY-MM-DD` | `2026-11-17` |
| Tenors | integer plus `D`, `W`, `M` or `Y` | `3M`, `10Y` |

**Inside `fit_core`:** rates, yields, coupons and spreads are decimals (`0.0425`); times are year
fractions; discount factors are dimensionless. Library functions never accept or return percent
or basis points.

Further rules:

- Output column and field names carry the unit where it is not obvious (for example a `_pct` or
  `_bp` suffix), so CSV and JSON consumers do not have to guess.
- Conventions that affect a number — day count, frequency, compounding, business-day convention,
  calendar, interpolation — are explicit parameters with documented defaults. The library has no
  hidden global convention state.
- Ambiguous input is rejected rather than guessed: non-ISO dates and malformed tenors are usage
  or data errors.

## Consequences

- Users interact in desk units; developers reason in one internal unit system; conversions are
  confined to a small, testable surface.
- A library consumer that bypasses the CLI must supply decimals. This is documented on the
  public headers.
- There is no heuristic to rescue a user who types `0.0425` for a coupon; it is taken as
  0.0425%. Echoing inputs in command output makes such mistakes visible.
- Unit-bearing column names are slightly longer.

## Alternatives considered

- **Decimals everywhere, including the CLI.** Consistent but hostile to desk users and
  error-prone when typing basis-point quantities. Rejected.
- **Unit suffixes on values (`4.25%`, `25bp`).** Self-describing, but awkward in shells and CSV
  files and at odds with how vendor exports look. Rejected for v0.1.
- **Strong unit types throughout the library.** Would catch mixing at compile time, at the cost
  of pervasive wrapper types in numerical code. Deferred; the boundary rule gives most of the
  benefit.

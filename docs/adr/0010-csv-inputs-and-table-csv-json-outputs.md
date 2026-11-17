# 0010. CSV inputs; table, CSV and JSON outputs

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

Inputs to the tool are tabular: a list of curve quotes, a list of positions, a list of proposed
trades. On a desk these originate in spreadsheets, vendor exports and position-system extracts.
Outputs are read by people at a terminal and by other programs — spreadsheets, scripts,
downstream reports. The tool should compose with standard Unix tooling and must not require a
database or service to be useful.

## Decision

**Inputs.**

- Multi-row inputs (curve quotes, positions, trades, custom holiday lists) are CSV files with a
  header row. Columns are identified by name, not position; names are case-insensitive and
  surrounding whitespace is ignored. Required columns are validated up front; optional columns
  have documented defaults. Blank lines and lines starting with `#` are ignored. Quoted fields
  follow RFC 4180.
- Single-instrument commands (`fit bond ...`) take the bond's terms as command-line options.
- Values use the boundary units of
  [ADR 0005](0005-units-and-conventions-at-cli-boundary.md).
- Data errors identify the file, the line and the column.

**Outputs.** Every command produces one or more named result tables through a single
intermediate representation, rendered according to the global `--format` option:

| Format | Purpose |
|--------|---------|
| `table` (default) | aligned, human-readable text for the terminal |
| `csv` | header row plus data rows, for spreadsheets and shell pipelines |
| `json` | structured output for programmatic consumers |

- `--precision` controls the number of decimal places for displayed values. It affects
  rendering only.
- Results go to standard output; diagnostics, warnings and errors go to standard error
  ([ADR 0011](0011-error-handling-and-exit-codes.md)), so output can always be piped or
  redirected safely.
- Column names and JSON field names are part of the CLI contract. Within the 0.x series they may
  change in a minor release, with a changelog entry.

The reader and writers are implemented in-tree
([ADR 0003](0003-zero-runtime-dependencies.md)).

## Consequences

- Any spreadsheet or extract can feed the tool, and any output can be diffed, versioned and
  post-processed with ordinary tools.
- Because all formats render from the same intermediate tables, they cannot disagree with each
  other, and a new command gets all three formats for free.
- CSV is untyped and flat. Schema validation is our responsibility, and nested structures (for
  example per-bond call schedules) will need either additional files or a richer format later.
- JSON is write-only in v0.1; there is no JSON input.
- Commands that emit several tables produce several consecutive blocks in `table` and `csv`
  output; consumers that need strict structure should use `json`.

## Alternatives considered

- **JSON or YAML inputs.** Better for nested data, worse for the tabular data desks actually
  have, and would need a parser. Rejected for v0.1.
- **FpML or other industry schemas.** Heavyweight relative to the scope. Rejected.
- **Database or market-data service connectivity.** Introduces dependencies and deployment
  coupling. Market-data adapters are on the roadmap behind the same CSV-shaped interface.
- **Excel files directly.** Requires a third-party library; CSV export is universal. Rejected.
- **Human-readable output only.** Forces consumers to scrape text. Rejected.

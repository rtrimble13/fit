# 0011. Error handling and exit codes

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

The worst failure of a pricing tool is a plausible wrong number. The second worst is an
uninformative failure in the middle of a batch run. The tool is used both interactively and from
scripts, so failures must be readable by a person and detectable by a program, and the library
must report problems in one consistent way.

## Decision

**In the library.**

- `fit_core` reports failure by throwing `fit::Error`, which derives from `std::exception` and
  carries a message with context: what was being done, the offending value, and where it came
  from (for input data: file, line and column; for the bootstrap: the instrument).
- Inputs are validated at construction and at parse time. Invalid dates, unknown conventions,
  non-positive frequencies, maturities before settlement, duplicate curve pillars, non-finite
  values and similar conditions are errors.
- Numerical failure is an error, never a best-effort answer: a solver that fails to bracket or
  converge throws, and NaN or infinity is never returned as a result.
- Exceptions are not used for control flow. Conditions that are expected in normal operation are
  expressed in return types (for example `std::optional`).
- Library code does not catch-and-ignore, does not write to the standard streams and does not
  terminate the process.

**In the CLI.**

- Exceptions are caught in exactly one place, at the top of `main`.
- Results are written to standard output; errors and warnings to standard error, prefixed
  `fit: error:` or `fit: warning:`.
- A failed command produces no partial result on standard output.
- Exit codes:

| Code | Meaning |
|------|---------|
| `0` | success |
| `1` | data or runtime error: unreadable or invalid input, validation failure, numerical failure |
| `2` | usage error: unknown command or option, missing or malformed argument |

## Consequences

- Scripts can distinguish "you called it wrongly" from "the data or the mathematics failed" and
  can rely on standard output being either complete or empty.
- Error messages are actionable because context is attached where it is known and enriched as
  the exception propagates.
- Failing fast means one bad row fails a whole portfolio run. This is intentional: a total that
  silently omits a position is worse than no total.
- All library functions must be exception-safe; the code base relies on RAII and value types to
  make this routine.
- Consumers embedding `fit_core` must be prepared to catch `fit::Error`.

## Alternatives considered

- **Error codes or `std::expected`-style returns throughout.** Explicit, but verbose in deeply
  nested numerical code, and `std::expected` is C++23. Rejected.
- **Skip bad rows and continue with a warning.** Convenient, but produces silently incomplete
  aggregates. Rejected as a default; an explicit opt-in could be considered later.
- **A richer exit-code taxonomy.** More codes than callers will realistically branch on. Three
  are sufficient and conventional.
- **Assertions/abort on invalid input.** Appropriate for programmer errors, not for user data.
  Rejected for input validation.

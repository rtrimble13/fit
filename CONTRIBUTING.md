# Contributing to fit

Thank you for contributing. `fit` produces numbers that inform trading decisions, so the bar for
correctness, explicitness and traceability is high. This guide explains how to set up, what
standards code must meet, and how changes are reviewed and released.

Related documents:

- [docs/PR_STANDARDS.md](docs/PR_STANDARDS.md) — what a pull request must contain to be merged.
- [docs/adr/](docs/adr/README.md) — architecture decision records; read these before changing
  methodology or structure.
- [docs/METHODOLOGY.md](docs/METHODOLOGY.md) — the definitions of every calculation.
- [docs/ROADMAP.md](docs/ROADMAP.md) — planned work and known limitations.

## Contents

1. [Getting started](#getting-started)
2. [Repository layout](#repository-layout)
3. [Development workflow](#development-workflow)
4. [Coding standards](#coding-standards)
5. [Numerical code rules](#numerical-code-rules)
6. [Testing requirements](#testing-requirements)
7. [Documentation requirements](#documentation-requirements)
8. [Commit conventions](#commit-conventions)
9. [Extension checklists](#extension-checklists)
10. [Architecture decision records](#architecture-decision-records)
11. [Review process](#review-process)
12. [Release process](#release-process)
13. [Reporting issues](#reporting-issues)

## Getting started

### Prerequisites

| Tool | Version |
|------|---------|
| C++ compiler | GCC 13+ or Clang 17+ |
| CMake | 3.24+ |
| Ninja | any recent |
| clang-format | 18 (the version CI uses) |
| clang-tidy | 18 (optional locally) |
| Python | 3.10+, standard library only: the release tool and its tests (skipped by `ctest` if absent) |
| make | optional: `make help` lists shortcuts such as `make check` and `make versions` |

Linux is the supported development platform. There are no third-party libraries to install: the
only vendored code is the doctest header in `third_party/`, and the build needs no network
access ([ADR 0003](docs/adr/0003-zero-runtime-dependencies.md)).

### Build and test

```sh
cmake --preset dev            # configure: Debug, ASan + UBSan, into build/dev
cmake --build --preset dev    # build fit_core, fit and the tests
ctest --preset dev            # run unit and CLI integration tests
```

Or in one line:

```sh
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
```

`make check` runs that, the formatting check and the version gate: what CI checks on a pull
request, with one compiler.

For an optimised build:

```sh
cmake --preset release && cmake --build --preset release && ctest --preset release
```

Useful variations:

```sh
ctest --preset dev --output-on-failure      # show output of failing tests
ctest --preset dev -R <regex>               # run tests whose name matches
./build/dev/fit --help                      # run the binary you just built
```

To build with a specific compiler, set `CXX` before configuring into a fresh build directory:

```sh
CXX=clang++-18 cmake --preset dev
```

### Formatting and static analysis

```sh
# Format everything you touched (CI rejects unformatted code)
find include src tests -name '*.hpp' -o -name '*.cpp' | xargs clang-format -i

# Check without modifying, exactly as CI does
find include src tests -name '*.hpp' -o -name '*.cpp' | xargs clang-format --dry-run --Werror

# Static analysis on a file (uses the compile database from the dev preset)
clang-tidy -p build/dev src/bond/<file>.cpp
```

Never format `third_party/`.

## Repository layout

```text
include/fit/<module>/*.hpp   public headers of fit_core
src/<module>/*.cpp           implementations
src/cli/                     argument parsing, command handlers, main
tests/unit/*.cpp             doctest unit tests against fit_core
tests/cli/                   integration tests that run the fit binary via ctest
examples/data/*.csv          example curves, portfolios and trades used by docs and tests
docs/adr/                    architecture decision records
docs/walkthroughs/           task-oriented walk-throughs
docs/METHODOLOGY.md          calculation definitions
third_party/doctest/         vendored test framework (test-only)
```

Modules and their allowed dependencies
([ADR 0004](docs/adr/0004-layered-library-plus-thin-cli.md)):

```text
core  <-  time  <-  math  <-  curve  <-  bond  <-  portfolio
core  <-  io   (leaf utility; usable by portfolio loaders and the CLI)
```

A module may include headers only from modules to its left. The CLI may use everything; nothing
in `fit_core` may include from `src/cli/`.

## Development workflow

Development is trunk-based with short-lived branches.

1. **Start from an issue** for anything non-trivial, so scope and methodology can be agreed
   before code is written. Changes to methodology or contracts need an ADR — see
   [below](#architecture-decision-records).
2. **Branch from `main`**: `<type>/<short-description>`, for example `feat/act-act-afb` or
   `fix/eom-schedule-february`. Keep branches short-lived; rebase on `main` rather than merging
   `main` in.
3. **Develop with the `dev` preset** so sanitizers run on every test.
4. **Version the change.** If it touches what fit ships (`src/`, `include/`, `CMakeLists.txt`,
   `CMakePresets.json`), bump `VERSION` as
   [docs/versioning-and-releases.md](docs/versioning-and-releases.md) describes, and add a
   `## [x.y.z] - YYYY-MM-DD` section to `CHANGELOG.md`. Run `make versions`. Changes that ship
   nothing need no bump.
5. **Open a pull request** early as a draft if you want feedback. It must meet
   [PR_STANDARDS.md](docs/PR_STANDARDS.md) before review is requested.
6. **Squash merge** once approved and green. The PR title becomes the commit on `main`. If the
   version was bumped, the release workflow then tags and publishes it; nobody tags by hand.

## Coding standards

### Language and style

- ISO C++20, no compiler extensions. Code must compile cleanly on GCC and Clang with
  `-Wall -Wextra -Wpedantic -Wconversion -Wshadow` as errors. Do not silence warnings with
  pragmas; fix the cause. Narrowing conversions must be explicit `static_cast`s.
- Layout is defined by `.clang-format` (LLVM-based, 100 columns, 4-space indent) and is not
  discussed in review.
- Everything lives in namespace `fit`. No `using namespace` in headers.

### Naming

| Entity | Style | Example |
|--------|-------|---------|
| Types (classes, structs, enums, aliases) | `PascalCase` | `YieldCurve`, `DayCount` |
| Functions and methods | `snake_case` | `year_fraction`, `discount_factor` |
| Variables and parameters | `snake_case` | `settle_date`, `clean_price` |
| Private and protected data members | `snake_case_` (trailing underscore) | `nodes_` |
| Enumerators | `PascalCase` | `DayCount::Act360` |
| Namespace-scope and `constexpr` constants | `kPascalCase` | `kMaxYield` |
| Namespaces | `lower_case` | `fit` |
| Files | `snake_case.hpp` / `.cpp` | `yield_curve.hpp` |
| Macros (avoid) | `FIT_UPPER_CASE` | |

Conventional mathematical names (`t`, `y`, `df`, `z`, `n`) are fine for local variables in
formulas where they match the methodology document. Names of quantities that have units at the
CLI boundary carry the unit: `shift_bp`, `coupon_pct`.

### Design

- **Value semantics first.** Prefer small immutable value types. Avoid shared mutable state;
  there is no global configuration.
- **Ownership is explicit.** No owning raw pointers; no `new`/`delete`. Use values,
  `std::unique_ptr`, or `std::shared_ptr` where sharing is genuinely needed (for example
  composed curves).
- **Const-correctness.** Mark everything `const` that can be; analytics functions do not mutate
  their inputs.
- **Interfaces.** Pass small types by value, larger ones by `const&`, ranges as `std::span`.
  Return results by value; use result structs rather than output parameters.
  Mark functions `[[nodiscard]]` where ignoring the result is a bug.
- **Enums over strings and booleans.** Conventions are `enum class` types; string parsing
  happens once, at the boundary.
- **Headers.** `#pragma once`, self-contained, include what you use, and keep implementation
  out of headers unless it is a template or trivially inline.
- **No I/O in pricing code.** Library modules other than `io` do not read files or write to
  streams.

### Error handling

Follow [ADR 0011](docs/adr/0011-error-handling-and-exit-codes.md):

- Report failure by throwing `fit::Error` with a message that says what was being done and
  which value was wrong. Include units and identifiers: prefer
  `"coupon frequency must be 1, 2, 4 or 12; got 3"` to `"invalid frequency"`.
- Validate inputs at construction/parse time so that objects are valid for their lifetime.
- Never return NaN, infinity or a sentinel for failure. A solver that does not converge throws.
- Do not use exceptions for expected outcomes; use `std::optional` or an explicit result type.
- Catch exceptions only to add context and rethrow, or at the top of `main`.

### Comments

Comment the *why*: the convention being implemented, the source of a formula, the reason for a
tolerance. Cite the standard or reference (for example "ISDA 2006 Definitions §4.16(b)").
Public headers document units, conventions, preconditions and what is thrown. Do not leave
commented-out code or unattributed TODOs; open an issue instead.

## Numerical code rules

These rules exist because numerical defects are usually silent.

1. **Units.** Inside `fit_core`, rates, yields, coupons and spreads are decimals; times are year
   fractions. Percent and basis points exist only at the CLI boundary
   ([ADR 0005](docs/adr/0005-units-and-conventions-at-cli-boundary.md)). A library function
   must never accept or return percent or basis points.
2. **Conventions are explicit.** Any function whose result depends on a day count, compounding,
   frequency, business-day convention, calendar or interpolation takes it as a parameter or
   from an object that states it. No implicit defaults buried in implementations; defaults are
   set at the boundary and documented.
3. **No magic numbers.** Tolerances, bump sizes, iteration limits and bracketing bounds are
   named `constexpr` constants with a comment explaining the choice. `365.0` and `360.0` belong
   in the day-count implementation and nowhere else.
4. **Tolerances are deliberate.** Solver tolerances must be several orders of magnitude tighter
   than any displayed precision. Document whether a tolerance is absolute or relative and in
   what unit.
5. **No floating-point equality** on computed values. Compare against a tolerance. Exact
   comparison is acceptable only for values that are exact by construction (for example a
   sentinel `0.0` coupon).
6. **Dates are integers.** Do date arithmetic on `Date`, not on year fractions. Never recover a
   date from a floating-point time.
7. **Guard the domain.** Check arguments to `log`, `sqrt`, `pow` and division; an invalid
   domain is a `fit::Error`, not a NaN that propagates.
8. **Numerical stability.** Prefer formulations that avoid cancellation (for example `expm1`,
   `log1p` for small arguments). Accumulate in a consistent order so results are deterministic.
9. **No rounding in the library.** Round only when rendering. Never round intermediate values.
10. **Root-finding.** Use the shared solvers in `math`. Supply a bracket justified by the
    problem; on failure to bracket or converge, throw with the instrument and inputs in the
    message.
11. **Finite differences.** Use the shared bump size and central differences
    ([ADR 0009](docs/adr/0009-risk-by-bump-and-reprice.md)). Do not introduce ad hoc bumps.
12. **No `-ffast-math`** or equivalent, in any target
    ([ADR 0006](docs/adr/0006-double-precision-numerics.md)).
13. **Additivity.** Position-level measures must sum to portfolio-level measures. If you add a
    measure that is not additive (a yield, a duration), define and document its aggregation
    explicitly.

## Testing requirements

The policy is set by
[ADR 0012](docs/adr/0012-testing-and-numerical-validation.md). In practice:

### What every change needs

| Change | Required tests |
|--------|----------------|
| New or changed pricing, curve or risk logic | Unit test with an **independently sourced** expected value and a **stated tolerance** |
| New convention (day count, calendar, business-day rule) | Reference cases from the governing standard, including edge cases |
| Bug fix | A regression test that fails without the fix |
| New or changed CLI command or option | CLI integration test covering success output and at least one error path with its exit code |
| Refactor | Existing tests pass unchanged; no expected value is edited |

### Independent sources

An expected value must not come from the code under test. Acceptable sources, in order of
preference:

1. A published reference: the governing standard's worked examples (for example the ISDA
   day-count examples) or documented examples of well-known functions (for example Excel
   `PRICE`, `YIELD`, `DURATION`, `MDURATION`).
2. An independent implementation, normally QuantLib. Record the version and the inputs.
   QuantLib is used on your machine only; it must not become a build or test dependency.
   `tools/quantlib_crosscheck.py` runs the existing comparisons against a built `fit` binary
   (`pip install QuantLib`); extend it when you add an analytic it can check.
3. A closed-form or hand calculation, shown in the test comment.

Cite the source next to the value:

```cpp
// Excel PRICE(DATE(2008,2,15), DATE(2017,11,15), 5.75%, 6.5%, 100, 2, 0) = 94.63436
CHECK(price == doctest::Approx(94.63436).epsilon(1e-7)); // relative; the reference has 7 digits
```

### Writing tests

- Unit tests go in `tests/unit/`, one file per library component, using doctest
  `TEST_CASE`/`SUBCASE`.
- State tolerances explicitly and choose them from the precision of the source: a reference
  quoted to five decimals supports a tolerance of `5e-6`, not `1e-12`. Never loosen a tolerance
  to make a test pass; find out why it fails.
- Add identity tests wherever an invariant exists: round-trips (price ↔ yield), repricing of
  bootstrap inputs, key-rate DV01s summing to parallel DV01, decompositions summing to totals.
- Cover edge cases deliberately: month-end and leap-day schedules, stub periods, settlement on
  a coupon date, zero coupons, negative rates, the first and last curve pillars and
  extrapolation.
- Tests are hermetic and deterministic: no network, no clock, no random seeds without fixing
  them, no dependence on the working directory beyond the paths ctest provides.
- CLI integration tests assert exit code, that results go to stdout and diagnostics to stderr,
  and output content at a precision robust to cross-compiler floating-point noise.

All tests must pass under the `dev` preset (ASan and UBSan) on both GCC and Clang.

## Documentation requirements

Documentation is part of the change, not a follow-up.

| If you change… | Update… |
|----------------|---------|
| A calculation or convention | `docs/METHODOLOGY.md` |
| A command, option, input schema or output column | `README.md` and the relevant walk-through in `docs/walkthroughs/` |
| A user-visible feature | Add or extend a walk-through with a runnable example using `examples/data/` |
| Anything user-visible | `CHANGELOG.md`: the new version's section, or `[Unreleased]` for a change that ships nothing |
| A methodology, dependency, layering or CLI-contract decision | A new ADR |
| A limitation or planned item | `docs/ROADMAP.md` |

Example output shown in documentation must be real output from the built tool, regenerated when
it changes. Public headers must document units, conventions and error conditions.

## Commit conventions

Commits and PR titles follow [Conventional Commits 1.0](https://www.conventionalcommits.org/):

```text
<type>(<scope>)<!>: <subject>

<body>

<footer>
```

**Types**

| Type | Use for |
|------|---------|
| `feat` | New user-visible capability |
| `fix` | Defect fix |
| `perf` | Performance improvement with no change in results |
| `refactor` | Internal restructuring with no behaviour change |
| `test` | Tests only |
| `docs` | Documentation only |
| `build` | Build system, presets, toolchain |
| `ci` | CI configuration |
| `chore` | Maintenance that fits nothing else |

**Scopes** are the module or area: `core`, `time`, `math`, `curve`, `bond`, `portfolio`, `io`,
`cli`, `docs`, `adr`.

**Rules**

- Subject in the imperative mood, lower case, no trailing full stop, at most 72 characters.
- The body explains what and why, wrapped at 100 columns. For numerical changes, state which
  outputs move and by how much.
- A breaking change to the CLI contract or the public API is marked with `!` and a
  `BREAKING CHANGE:` footer.
- Reference issues in the footer: `Closes #42`.

Examples:

```text
feat(time): add ACT/365L day count

fix(bond): accrue over the regular period for short first coupons

Accrued interest under ACT/ACT ICMA used the stub length as the reference
period. Use the notional regular period ending on the first coupon date.
Changes accrued by up to 0.002 per 100 for bonds inside a short first
period; verified against QuantLib 1.36.

Closes #57

feat(cli)!: rename --bump to --bump-bp

BREAKING CHANGE: the --bump option of `fit bond risk` is now --bump-bp.
```

Because PRs are squash-merged, the PR title and description become the commit on `main`;
individual commits on a branch need only be coherent enough to review.

## Extension checklists

Follow the existing pattern in each area; these lists make sure nothing is missed.

### Adding a day-count convention

- [ ] Add the enumerator to the day-count enum in `include/fit/time/`.
- [ ] Implement day count and year fraction in `src/time/`; if the convention needs reference
      periods or a frequency (as ACT/ACT ICMA does), make that explicit in the interface.
- [ ] Add the parsing aliases and the canonical display name; reject ambiguous aliases.
- [ ] Unit tests from the governing standard's examples, plus: end-of-month dates, 29 February,
      periods crossing a leap-year boundary, `d1 == d2`, and reversed dates.
- [ ] Verify accrued interest for a bond using the convention against an independent source.
- [ ] Document the definition and source in `docs/METHODOLOGY.md`; list it in `README.md`.
- [ ] `CHANGELOG.md` entry.

### Adding an interpolation scheme

- [ ] Implement the interpolator in `math`, independent of curve concepts, with unit tests on
      known functions: exact reproduction at nodes, exact reproduction of functions the scheme
      should reproduce (for example linear data), behaviour with two nodes and with one node.
- [ ] Wire it into the curve as a named scheme; add the CLI name to the parser and help text.
- [ ] State and test the extrapolation behaviour.
- [ ] Confirm the bootstrap converges and reprices all instruments with the new scheme on the
      example curves; note whether the scheme is local or global.
- [ ] Test forward-rate properties the scheme claims (positivity, continuity) on a curve that
      stresses them, and behaviour under negative rates.
- [ ] Cross-check discount factors against an independent implementation.
- [ ] Document in `docs/METHODOLOGY.md` and the curve walk-through, including when to use it.
- [ ] If it is proposed as the new default, write an ADR superseding the relevant part of
      [ADR 0007](docs/adr/0007-curve-representation-and-interpolation.md).
- [ ] `CHANGELOG.md` entry.

### Adding a curve quote type

- [ ] Define the instrument's conventions and defaults explicitly (day count, frequency,
      settlement lag, business-day convention).
- [ ] Implement its repricing function for the bootstrap: zero when the curve reprices the
      quote.
- [ ] Add the type name to the quotes-file parser with validation and clear errors.
- [ ] Tests: bootstrap reprices the instrument under every interpolation scheme; resulting
      discount factors agree with an independent implementation.
- [ ] Document the conventions in `docs/METHODOLOGY.md` and the quotes-file schema in
      `README.md`.
- [ ] `CHANGELOG.md` entry.

### Adding an instrument type

- [ ] Write an ADR if the instrument needs a model or convention choice (for example optionality
      or a new yield convention).
- [ ] Define the instrument in `include/fit/bond/` as a validated value type; all terms and
      conventions explicit.
- [ ] Implement cash-flow generation first and test it against an independent schedule.
- [ ] Implement valuation off a curve, then yield/spread measures, then risk. Curve risk must
      go through the shared bump-and-reprice machinery.
- [ ] Ensure the instrument works in horizon analysis and in portfolio aggregation, or reject
      it there with a clear error — never silently mis-handle it.
- [ ] Golden-value tests against an independent source for price, accrued, yield and risk,
      including stubs and month-end cases; identity tests (round-trips, additivity).
- [ ] Extend the positions CSV schema if needed, keeping existing files valid.
- [ ] CLI support, integration tests, `docs/METHODOLOGY.md`, a walk-through with example data,
      `README.md`, `docs/ROADMAP.md` and `CHANGELOG.md`.

### Adding a CLI command or option

- [ ] Put the calculation in `fit_core` with unit tests. The command handler only parses,
      converts units, calls the library and renders.
- [ ] Follow existing naming: `fit <group> <verb>`, long options in `--kebab-case`, units as in
      [ADR 0005](docs/adr/0005-units-and-conventions-at-cli-boundary.md) with unit suffixes on
      option and column names where needed (`--bump-bp`, `yield_pct`).
- [ ] Validate arguments up front; usage problems exit `2`, data and runtime problems exit `1`.
- [ ] Render through the shared table representation so that `table`, `csv` and `json` all work
      and `--precision` is honoured. Results to stdout only; nothing else on stdout.
- [ ] Write `--help` text with units, defaults and an example.
- [ ] CLI integration tests: success in each output format, a usage error and a data error.
- [ ] Update `README.md`, add or extend a walk-through, and add a `CHANGELOG.md` entry. A change
      to an existing command's contract is a breaking change in the changelog.

## Architecture decision records

Significant decisions are recorded in [docs/adr/](docs/adr/README.md). You need an ADR when a
change:

- alters a numerical methodology or market convention;
- adds or removes a dependency, or changes the supported toolchain;
- changes module layering or a public interface contract of `fit_core`;
- changes the CLI contract (commands, units, schemas, exit codes);
- reverses or amends an existing ADR.

To write one: copy `docs/adr/template.md` to the next number, set the status to `Proposed`, and
submit it in a PR — with the implementation, or ahead of it for larger changes. On merge the
status becomes `Accepted` and the index in `docs/adr/README.md` is updated. Accepted ADRs are
not edited; a changed decision gets a new ADR that supersedes the old one.

## Review process

- Every change to `main` goes through a pull request and needs at least one approving review
  from a maintainer. Changes to pricing, curve or risk logic need a reviewer who has checked the
  numbers, not only the code.
- The author is responsible for making the PR reviewable: small, single-purpose, with the
  description and evidence required by [PR_STANDARDS.md](docs/PR_STANDARDS.md).
- Reviewers work through the reviewer checklist in that document. Layout is delegated to
  `clang-format`; review time goes on correctness, conventions, tests and clarity.
- Feedback is addressed with follow-up commits (not force-pushes of rewritten history) so that
  reviewers can see what changed; the branch is squashed on merge.
- Disagreements about methodology are resolved by evidence — a standard, an independent
  implementation, a worked example — and, where the outcome is a decision, recorded in an ADR.
- Aim to respond to review requests within two working days.

## Release process

Releases are continuous and automated
([ADR 0018](docs/adr/0018-continuous-versioning-and-automated-releases.md)). The full rules are
in [docs/versioning-and-releases.md](docs/versioning-and-releases.md). In short:

- `fit` follows [Semantic Versioning](https://semver.org/). The version lives only in
  [`VERSION`](VERSION). MAJOR covers incompatible CLI or API changes, and any change to
  computed numbers for existing inputs. MINOR covers new capability, and PATCH covers
  corrections that change no output. Before 1.0.0, MINOR also carries breaking and numerical
  changes.
- Every pull request that changes a shipped file bumps `VERSION` and adds a matching dated
  section to `CHANGELOG.md`. The *Version and changelog* CI job enforces this; run
  `make versions` locally.
- A release that changes a computed number for existing inputs says so in a **Numerical
  changes** section of its changelog entry, with the reason and the size of the effect. A fix
  that changes numbers is never released silently, however small.
- Merging to `main` releases the new version. The release workflow builds and tests it,
  creates the tag `v<version>` and publishes a GitHub release with the Linux binary archive,
  its checksum and the changelog section as notes. `make release-plan` previews that.
- Tags are never moved or deleted. A bad release is fixed forward with a new version.

## Reporting issues

Use the issue templates. For a suspected wrong number, include the exact command, the input
files (or a minimal reproduction), the output, the value you expected, and where the expected
value comes from. An independent reference turns a discussion into a fix.

Do not include confidential positions or proprietary market data in issues or test fixtures; use
synthetic or public data.

## Licence

`fit` is released under the MIT License. By contributing you agree that your contributions are
licensed under the same terms.

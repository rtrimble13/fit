# Pull Request Standards

These standards define what a pull request to `fit` must look like to be reviewed and merged.
They complement [CONTRIBUTING.md](../CONTRIBUTING.md), which covers how to build, code and test.
The [pull request template](../.github/pull_request_template.md) mirrors this document.

The standards are strict where a mistake could change a number without anyone noticing, and
light everywhere else.

## Contents

1. [Scope and size](#scope-and-size)
2. [Title](#title)
3. [Description](#description)
4. [Required checks](#required-checks)
5. [Numerical-change policy](#numerical-change-policy)
6. [CLI contract changes](#cli-contract-changes)
7. [Reviewer checklist](#reviewer-checklist)
8. [Review etiquette](#review-etiquette)
9. [Merge policy](#merge-policy)
10. [Definition of done](#definition-of-done)

## Scope and size

- **One purpose per PR.** A PR does one thing: one feature, one fix, one refactor. If the
  description needs the word "also", split it.
- **Separate behaviour from structure.** Refactors and formatting-only changes go in their own
  PRs, so that a behaviour-changing diff contains only behaviour changes and a refactor PR can
  be verified by "no test expectations changed".
- **Keep it reviewable.** Aim for under about 400 changed lines of non-test, non-data code.
  Larger work is delivered as a sequence of PRs that each leave `main` working. Generated or
  bulk data files (example CSVs, golden values) do not count, but must be explained.
- **Vertical slices.** A feature PR includes its library code, tests, CLI wiring and
  documentation together. Do not merge untested code with a promise of tests to follow.
- **Draft early.** Open a draft PR for feedback on direction; mark it ready only when it meets
  this standard.

## Title

The title follows [Conventional Commits](https://www.conventionalcommits.org/) and becomes the
squash-commit subject on `main`:

```text
<type>(<scope>)<!>: <imperative summary>
```

- **type:** `feat`, `fix`, `perf`, `refactor`, `test`, `docs`, `build`, `ci`, `chore`.
- **scope:** `core`, `time`, `math`, `curve`, `bond`, `portfolio`, `io`, `cli`, `docs`, `adr`.
- **`!`** marks a breaking change to the CLI contract or public API.
- Imperative mood, lower case, no trailing full stop, at most 72 characters.

| Good | Not acceptable |
|------|----------------|
| `feat(curve): add flat-forward extrapolation option` | `Curve changes` |
| `fix(bond): use regular period for ICMA accrual in short first coupon` | `Fixed bug` |
| `refactor(portfolio): extract key-rate aggregation` | `WIP` |
| `feat(cli)!: rename --shift to --shift-bp` | `Update main.cpp` |

## Description

Every PR description contains the following. Sections that do not apply say "None" rather than
being deleted, so that reviewers know the question was considered.

1. **Summary** — what changes and why, in a few sentences, with a link to the issue
   (`Closes #N`).
2. **Type of change** — bug fix, feature, refactor, performance, documentation, build/CI.
3. **Numerical impact** — exactly one of:
   - *None*: no computed output changes for any existing input;
   - *New outputs only*: existing outputs are unchanged;
   - *Existing outputs change*: with the before/after evidence required by the
     [numerical-change policy](#numerical-change-policy).
4. **Validation** — how correctness was established: the independent sources of expected
   values, the tolerances used, and any manual checks performed.
5. **CLI contract** — whether commands, options, input schemas, output columns or fields, units
   or exit codes change; if so, how.
6. **Checklist** — the author checklist from the template, completed truthfully.
7. **Notes for reviewers** — where to start reading, trade-offs made, and follow-up work
   deliberately left out.

For user-visible changes, include a short before/after sample of the command and its output.

## Required checks

All of the following must pass before merge. The first four are enforced by CI; the rest are
confirmed by the author and verified in review.

| Check | Requirement |
|-------|-------------|
| Build | `dev` and `release` presets build on GCC and Clang with warnings as errors |
| Tests | `ctest` passes for both presets on both compilers; the `dev` preset runs under AddressSanitizer and UndefinedBehaviorSanitizer |
| Formatting | `clang-format --dry-run --Werror` is clean over `include/`, `src/` and `tests/` |
| Up to date | Branch is rebased on current `main` with no conflicts |
| Static analysis | No new `clang-tidy` findings in changed code, or each one justified in the PR |
| Tests added | New behaviour is covered; bug fixes include a test that fails without the fix |
| Documentation | README, methodology, walk-throughs and changelog updated as required by CONTRIBUTING.md |
| ADR | Present where the change meets the ADR criteria |
| Dependencies | No new third-party dependency without an accepted ADR |

Checks must not be made to pass by weakening them. In particular, a PR must not: disable,
skip or delete a failing test; loosen a tolerance without justification; suppress a warning or
sanitizer report; or exclude files from formatting.

## Numerical-change policy

A **numerical change** is any change after which some existing input produces a different
computed output — price, yield, accrued, discount factor, risk measure, P&L, cash-flow date or
amount — beyond floating-point noise. A change to any golden value in the test suite is, by
definition, a numerical change.

Numerical changes are permitted only under these conditions:

1. **Declared.** The PR is marked "Existing outputs change" and the title and changelog entry
   make the change visible. Numerical changes are never bundled into a PR described as a
   refactor, clean-up or performance improvement.
2. **Explained.** The description states the cause: which defect is fixed or which methodology
   is changed, and why the old value was wrong or is being replaced.
3. **Quantified.** A before/after table lists the affected quantities on representative cases
   with the size of the difference in natural units (price per 100, basis points of yield,
   currency DV01), and states which inputs are affected and which are not.
4. **Independently validated.** The new value is supported by a source independent of the
   implementation: a published reference, an independent implementation such as QuantLib (with
   version and inputs), or a closed-form or hand calculation shown in the PR. "The new code
   produces it" is not validation. Agreement with the independent source must be within a stated
   tolerance, and any remaining difference explained.
5. **Minimal.** Only the golden values that the change necessarily affects are updated. Each
   updated value keeps or gains a comment citing its source. Golden values are never
   regenerated in bulk from the tool's own output.
6. **Recorded.** `CHANGELOG.md` lists the change under **Numerical changes**, with reason and
   magnitude. A change of methodology (as opposed to a defect fix) also needs an ADR.
7. **Reviewed for numbers.** At least one approving reviewer has checked the validation
   evidence itself — reproduced or inspected the independent calculation — not only read the
   code.

Tolerance changes are numerical changes in miniature: loosening a test tolerance requires an
explanation of why the previous tolerance was unjustified, not merely that the test now fails.

If a difference is at the level of floating-point noise (for example from reordering
operations), say so, show its magnitude, and confirm that no test tolerance needed to change.

## CLI contract changes

The CLI contract comprises command and option names, units, input file schemas, output column
and field names, stream usage and exit codes
(ADRs [0005](adr/0005-units-and-conventions-at-cli-boundary.md),
[0010](adr/0010-csv-inputs-and-table-csv-json-outputs.md) and
[0011](adr/0011-error-handling-and-exit-codes.md)).

- Additive changes (a new command, option, optional input column or output column) are
  non-breaking but must be documented.
- Renaming or removing anything, changing a unit or a default, or changing an exit code is
  breaking: mark the title with `!`, add a `BREAKING CHANGE:` note to the description, list it
  in the changelog, and update every walk-through and example that uses it.
- Changing a default convention is both a contract change and a numerical change.

## Reviewer checklist

Reviewers confirm each of the following, or say why it does not apply.

**Correctness and methodology**
- [ ] The change does what the description says, and nothing else.
- [ ] Formulas match `docs/METHODOLOGY.md` and the cited standard; conventions (day count,
      compounding, frequency, calendar, settlement) are the right ones and are explicit.
- [ ] Units are decimal/year-fraction inside the library; conversion happens only at the CLI
      boundary.
- [ ] Edge cases are handled: stubs, month-end and leap dates, settlement on a coupon date,
      zero coupons, negative rates, extrapolation, empty and single-row inputs.
- [ ] Invalid input and numerical failure raise `fit::Error` with useful context; nothing
      returns NaN, a sentinel or a silently truncated result.

**Numerical integrity**
- [ ] The declared numerical impact is accurate. If "None", no golden value or tolerance
      changed.
- [ ] If outputs change, the numerical-change policy is met and the validation evidence has
      been examined.
- [ ] No magic numbers; tolerances and bump sizes are named constants with rationale.
- [ ] No floating-point equality on computed values; no rounding inside the library.

**Tests**
- [ ] New logic has tests with independently sourced, cited expected values and justified
      tolerances.
- [ ] Bug fixes have a regression test that would fail without the fix.
- [ ] Identity/invariant tests are present where an invariant exists.
- [ ] CLI changes have integration tests covering success and error paths and exit codes.
- [ ] Tests are deterministic and hermetic.

**Design and code quality**
- [ ] Module layering is respected; no financial logic in `src/cli/`; no I/O in pricing code.
- [ ] Interfaces are const-correct, use value types and enums, and are hard to misuse.
- [ ] Naming and structure follow CONTRIBUTING.md; the code is as simple as the problem allows.
- [ ] No new dependency; no warning suppressions; no commented-out code.

**Contract and documentation**
- [ ] CLI contract changes are identified, justified and flagged as breaking where applicable.
- [ ] Results go to stdout, diagnostics to stderr; exit codes follow ADR 0011.
- [ ] README, methodology, walk-throughs and `--help` reflect the change; documented example
      output is real.
- [ ] `CHANGELOG.md` is updated; an ADR is present if required.

## Review etiquette

- **Authors:** review your own diff first. Respond to every comment, by change or by reply.
  Push follow-up commits rather than rewriting history during review. Do not resolve a thread
  you did not open unless the reviewer agreed.
- **Reviewers:** review within two working days. Distinguish blocking issues from suggestions;
  prefix optional remarks with `nit:`. Ask for evidence rather than asserting on methodology
  questions, and supply evidence when you disagree. Approve only what you have actually checked,
  and say what you did not check.
- **Both:** layout is settled by `clang-format`. Disputes on methodology are settled by a
  standard, an independent implementation or a worked example, and recorded in an ADR if they
  amount to a decision.

## Merge policy

- **Approvals.** At least one approving review from a maintainer other than the author.
  Changes to pricing, curve or risk logic, and any numerical change, require that an approver
  has verified the numbers. ADRs require maintainer approval.
- **Green.** All required checks pass on the final commit. Approval is re-requested after
  substantive changes.
- **Resolved.** All review threads are resolved.
- **Squash merge only.** `main` has linear history with one commit per PR. The squash commit
  uses the PR title as its subject and a cleaned-up description as its body.
- **Who merges.** The author merges once requirements are met, or a maintainer does for
  external contributors.
- **No direct pushes** to `main`, and no force-pushes to `main`.
- **Branch cleanup.** The branch is deleted after merge.
- **Reverts.** If a merged change is found to produce wrong numbers or break `main`, revert
  first, then fix forward in a new PR.

## Definition of done

A change is done when all of the following are true:

1. It meets the scope, title and description standards above.
2. All required checks pass on GCC and Clang, in `dev` (with sanitizers) and `release`.
3. New behaviour is tested with independently sourced expected values and stated tolerances;
   existing golden values are untouched or changed under the numerical-change policy.
4. The numerical impact is declared and, if non-zero, explained, quantified, validated and
   recorded.
5. User-facing documentation, `--help` text, walk-throughs and example data match the
   behaviour.
6. `CHANGELOG.md` has an entry under `[Unreleased]`.
7. An ADR exists for any methodology, dependency, layering or contract decision.
8. No known defects, skipped tests or unexplained warnings remain; deliberately deferred work is
   captured in an issue and, where relevant, in `docs/ROADMAP.md`.
9. It has the required approvals and has been squash-merged to `main`.

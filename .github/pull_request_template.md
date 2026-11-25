<!-- Title: Conventional Commits format, e.g. "feat(curve): add flat-zero extrapolation".
     See docs/PR_STANDARDS.md for the full standard. -->

## Summary

<!-- What changes and why, in a few sentences. Link the issue: "Closes #123". -->

## Type of change

- [ ] Bug fix
- [ ] New feature
- [ ] Refactor (no behaviour change)
- [ ] Performance
- [ ] Documentation only
- [ ] Build / CI / tooling

## Version

<!-- See docs/versioning-and-releases.md. A change to src/, include/, CMakeLists.txt,
     CMakePresets.json or VERSION needs a bump; merging it publishes the release. -->

- [ ] **No bump**: no shipped file changes.
- [ ] **Bumped**: complete the table.

| Old version | New version | Why this bump (MAJOR / MINOR / PATCH) |
|-------------|-------------|---------------------------------------|
|             |             |                                       |

## Numerical impact

- [ ] **None** — no computed output changes for any existing input.
- [ ] **New outputs only** — existing outputs unchanged.
- [ ] **Existing outputs change** — complete the table below.

<!-- If existing outputs change: which quantities, for which inputs, by how much, and why the
     new value is right. State the independent source for the new value. -->

| Quantity | Case | Before | After | Difference | Independent source |
|----------|------|--------|-------|------------|--------------------|
|          |      |        |       |            |                    |

## Validation

<!-- How was correctness established? Cite independent sources for expected values (published
     reference, QuantLib, closed form / hand calculation) and the tolerances used. -->

## CLI contract

- [ ] No change to commands, options, input schemas, output columns/fields, units or exit codes.
- [ ] Contract changes — described here and in `CHANGELOG.md`:

## Checklist

- [ ] `cmake --preset dev && cmake --build --preset dev && ctest --preset dev` passes locally.
- [ ] Code is formatted with `clang-format`; no new compiler warnings.
- [ ] Tests added or updated; new expected values are independently sourced, cited and have a
      stated tolerance.
- [ ] Conventions and units are explicit; no new magic numbers or inline tolerances.
- [ ] Documentation updated (README, `docs/METHODOLOGY.md`, walk-throughs) where behaviour or
      usage changed.
- [ ] `make versions` passes: if shipped files changed, `VERSION` is bumped and `CHANGELOG.md`
      has a dated section for it; otherwise any user-visible note is under `[Unreleased]`.
- [ ] ADR added or superseded if this changes methodology, dependencies, layering or the CLI
      contract.
- [ ] No new third-party dependency (or an accepted ADR covers it).

## Notes for reviewers

<!-- Where to start, known trade-offs, follow-up work deliberately left out. -->

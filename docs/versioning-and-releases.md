# Versioning and releases

`fit` is versioned with [Semantic Versioning](https://semver.org/). Every pull request that changes
what fit ships bumps the version, and merging it to `main` publishes the release automatically.
The decision is recorded in [ADR 0018](adr/0018-continuous-versioning-and-automated-releases.md).

## Contents

- [What is versioned](#what-is-versioned)
- [What MAJOR, MINOR and PATCH mean for fit](#what-major-minor-and-patch-mean-for-fit)
- [The changelog](#the-changelog)
- [The version gate in CI](#the-version-gate-in-ci)
- [How a release happens](#how-a-release-happens)
- [Pre-releases](#pre-releases)
- [Versions and roadmap milestones](#versions-and-roadmap-milestones)
- [Fixing a bad release](#fixing-a-bad-release)
- [Recommended repository settings](#recommended-repository-settings)

## What is versioned

| Thing | Version lives in | Example | Bumped when |
|---|---|---|---|
| fit: the `fit_core` library and the `fit` CLI | [`VERSION`](../VERSION) at the repository root | `0.2.0` | Any change to a shipped file: `src/`, `include/`, `CMakeLists.txt`, `CMakePresets.json` or `VERSION` itself |
| Documentation, tests, examples, tools and CI | Not versioned | | Never. Note user-visible ones under `[Unreleased]` in the changelog |

`VERSION` is the single source of the version. CMake reads it into `project()` and into the
`FIT_VERSION_STRING` that `fit --version` prints, and the release tool reads it to tag and
name releases. A change to `VERSION` re-runs CMake by itself. Never write the version anywhere
else.

## What MAJOR, MINOR and PATCH mean for fit

fit's contract with its users is the CLI contract, the `fit_core` public API, and the numbers
it computes. The CLI contract covers commands, options, units, input schemas, output columns and
fields, stream use and exit codes ([PR_STANDARDS.md](PR_STANDARDS.md#cli-contract-changes)).
Bump for the strongest effect a change has on that contract:

| Bump | Meaning | Examples |
|---|---|---|
| **MAJOR** (`2.0.0`) | An incompatible change, or a change to existing numbers | Renaming or removing a command, option, output column or input column. Changing a unit, a default convention or an exit code. A breaking change to the `fit_core` API. Any **numerical change**: an existing input gives a different computed output, whether from a methodology change or a defect fix ([numerical-change policy](PR_STANDARDS.md#numerical-change-policy)). |
| **MINOR** (`1.1.0`) | New capability; every existing input gives the same output | A new command, option, quote type, interpolation scheme, output column or optional input column. A new additive `fit_core` function. |
| **PATCH** (`1.0.1`) | Corrections with no change to any computed output or to the contract | A crash or wrong error message fixed. Clearer `--help` text. A build fix. A performance improvement with identical results. |

A defect fix that moves numbers is MAJOR. It is a fix, but users comparing results before and
after need to know that the numbers changed. Say so in the changelog's **Numerical changes**
section, with the size of the effect.

**Before 1.0.0** fit is in development and `0.y.z` versions may change in any way. MINOR bumps
carry both breaking and numerical changes and new features; PATCH bumps carry the rest. Breaking
and numerical changes are still called out in the changelog. From 1.0.0 the table above
applies in full.

## The changelog

[CHANGELOG.md](../CHANGELOG.md) follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/):

```markdown
## [Unreleased]

## [0.3.0] - 2026-12-15

One sentence on what this version is about, and whether outputs change.

### Added

- `fra` quote type for forward-rate agreements on projection curves (METHODOLOGY.md, section 4).

### Numerical changes

- ...
```

The release tool enforces these rules, in `validate`, in the `release-tool` ctest and in CI:

- An optional `## [Unreleased]` section comes first.
- Every version has a section `## [x.y.z] - YYYY-MM-DD` with a real date. Sections are listed
  newest first.
- The newest version section matches `VERSION`.

Use the standard categories, **Added**, **Changed**, **Deprecated**, **Removed**, **Fixed** and
**Security**, plus fit's own **Numerical changes**. Write for users: say what fit now does
differently, and point to the methodology section or ADR. A version's section becomes the text
of its GitHub release.

Changes that ship nothing, such as documentation, tests and CI, need no version. If users
should hear about one, note it under `[Unreleased]`. It then appears in the next version's
section.

## The version gate in CI

On every pull request, the *Version and changelog* job runs
`tools/release.py check-versions` against the base branch:

1. It lists the files the pull request changes, including uncommitted and untracked ones when
   run locally.
2. If any shipped file changed, it requires two things:
   - `VERSION` is greater than the version on the base branch, under SemVer precedence;
   - `CHANGELOG.md` has a section for the new version.
3. If no shipped file changed, it passes with no bump.

Run it locally before pushing:

```sh
make versions
```

Without make: `python3 tools/release.py validate`, then
`python3 tools/release.py check-versions --base origin/main`.

The gate checks that a bump happened, not that it is the right size. Choosing MAJOR, MINOR or
PATCH is the author's job, and checking it is the reviewer's.

## How a release happens

```text
PR with a VERSION bump ──► CI passes ──► merge to main ──► release workflow
                                                            1. validates VERSION and CHANGELOG.md
                                                            2. builds and tests the release preset
                                                            3. checks that fit --version prints VERSION
                                                            4. if the tag v<version> is missing:
                                                               builds fit-<version>-linux-x86_64.tar.gz
                                                               and its .sha256, then creates the tag and
                                                               the GitHub release, with the changelog
                                                               section as release notes
```

- **Tag:** `v<version>`, for example `v0.2.0`, at the merge commit.
- **Assets:** `fit-0.2.0-linux-x86_64.tar.gz` contains `fit-0.2.0/bin/fit` and `LICENSE`.
  `fit-0.2.0-linux-x86_64.tar.gz.sha256` is its checksum, for `sha256sum -c`. The archive is
  deterministic (fixed timestamps, owners and order). The binary is built on Ubuntu 24.04 with
  GCC 13.
- **Idempotent:** a version that already has its tag is skipped. Re-running the workflow, or
  merging a change that ships nothing, releases nothing.
- **Trigger:** a push to `main` that changes `VERSION`. The workflow can also be started by hand
  from the Actions tab (`workflow_dispatch`), for example after a transient failure.

To preview what the next merge would release, including the exact `gh` command and notes:

```sh
make release-plan
```

Other commands are `make package` (build the archive into `dist/`) and
`python3 tools/release.py notes [--version X.Y.Z]` (print a changelog section). Nobody tags by
hand.

## Pre-releases

A version with a pre-release suffix, such as `0.3.0-rc.1`, is published as a GitHub
**pre-release**. Under Semantic Versioning `0.3.0-rc.1` sorts before `0.3.0`, and the version
gate respects that, so `0.2.0 → 0.3.0-rc.1 → 0.3.0` is a valid sequence. Use a pre-release to
let users try a larger change, such as a new default convention, before it is final.

`main` then holds the pre-release until the final version is merged. If that is not wanted,
keep the change on its branch until it is final.

## Versions and roadmap milestones

The [roadmap](ROADMAP.md) is organised by milestone (M2 Curve depth, M3 Instrument coverage, and
so on). Milestones are not versions. A milestone spans as many versions as it takes, and is
reached when its exit criteria are met. The only version tied to the roadmap is 1.0.0, which is
released when the stability commitment is met.

Version 0.1.0 predates automated releases and has no tag. To have it on the releases page, tag
the PR #1 merge commit (`ea0e2b1`) by hand as `v0.1.0`. That is the one exception to "nobody
tags by hand".

## Fixing a bad release

Releases are immutable. Do not delete or move tags, because users and automation may have
pinned them.

1. Fix forward. Open a pull request with the fix and a new version, PATCH or (if numbers move)
   MAJOR (MINOR before 1.0), and merge it. The release workflow publishes it.
2. Mark the bad release on GitHub. Edit its notes to point to the fixed version, and, if it is
   harmful, tick "Set as a pre-release" so that it no longer shows as the latest.
3. Explain what was wrong in the new version's changelog section, under **Fixed** (and
   **Numerical changes** if numbers move).

## Recommended repository settings

These are set in GitHub, not in files:

- **Branch protection on `main`:** require a pull request, an approving review, and these
  status checks: the four build jobs (`gcc-13 / dev`, `gcc-13 / release`, `clang-18 / dev`,
  `clang-18 / release`), *clang-format*, and *Version and changelog*. Disallow force pushes.
- **Tag protection:** protect `v*` tags from deletion and update, so that releases stay
  immutable.
- **Actions → Workflow permissions:** "Read repository contents" is enough. The release job asks
  for `contents: write` itself.

# 0018. Continuous versioning and automated releases

- **Status:** Proposed
- **Date:** 2026-11-25

## Context

Until now a release was a manual event. When a roadmap release's exit criteria were met, a
maintainer opened a `chore(release)` pull request, set the version in `CMakeLists.txt`, moved
the `[Unreleased]` changelog entries into a dated section, tagged the merge commit and wrote the
release notes (CONTRIBUTING.md, "Release process"). The roadmap's themes were named after the
versions they would become (v0.2 Curve depth, v0.3 Instrument coverage, and so on).

In practice this left gaps:

- Merged work, including a declared numerical change (the swap accrual fix), sat under
  `[Unreleased]` with no version a user could name.
- Version 0.1.0 was never tagged.
- The version was written in two places, `CMakeLists.txt` and `include/fit/version.hpp`.
- Nothing checked that the changelog matched the code.

A user who reports a wrong number, or pins a binary, needs the version to identify exactly what
they ran.

The sibling repository `rtrimble13/alpha-tango` solved the same problem for its skills, which
are released continuously. Every change to a shipped file bumps a SemVer version and adds a
validated changelog section. CI gates pull requests on that, and merging to `main` tags and
publishes the release with checksummed assets. fit should follow the same model, so the two
repositories are maintained the same way.

## Decision

We will version fit continuously and release it automatically, following the alpha-tango model,
adapted to a single C++ product.

- **One version, one place.** A `VERSION` file at the repository root holds the SemVer version,
  including any pre-release suffix. CMake reads it into `project()` and into the
  `FIT_VERSION_STRING` that `fit --version` prints. No other file states the version.
- **What ships.** `src/`, `include/`, `CMakeLists.txt`, `CMakePresets.json` and `VERSION`.
  Documentation, tests, examples, tools and CI ship nothing.
- **Bump rules** ([versioning and releases](../versioning-and-releases.md)):
  - MAJOR for an incompatible CLI or API change, or for any numerical change to existing
    outputs, including a defect fix that moves numbers.
  - MINOR for new capability.
  - PATCH for corrections that change no output or contract.
  - Before 1.0.0, MINOR carries breaking and numerical changes as well as features.
- **Changelog.** `CHANGELOG.md` stays in Keep a Changelog format with fit's **Numerical
  changes** category. `tools/release.py validate` enforces the structure:
  - `[Unreleased]` optional and first;
  - dated `## [x.y.z] - YYYY-MM-DD` sections, newest first;
  - the newest section equal to `VERSION`.
- **Version gate.** On every pull request, CI runs `tools/release.py check-versions` against the
  base branch. If any shipped file changed, `VERSION` must be greater than the base version and
  the changelog must have its section.
- **Automated release.** A push to `main` that changes `VERSION` runs the release workflow. It
  validates and tests, checks that the binary prints `VERSION`, then publishes if the tag
  `v<version>` does not exist yet:
  - it creates the tag at the merge commit;
  - it attaches a deterministic `fit-<version>-linux-x86_64.tar.gz` (the binary and the
    licence) and its `.sha256`;
  - it uses the version's changelog section as the release notes.

  Re-running the workflow is harmless, a `-rc.N` version becomes a GitHub pre-release, and
  tags are immutable: a bad release is fixed forward.
- **Tooling.** `tools/release.py` uses only the Python standard library and is a development
  tool, not a build dependency ([ADR 0003](0003-zero-runtime-dependencies.md)). Its `unittest`
  suite runs under `ctest` as `release-tool`, and includes the validation of the real `VERSION`
  and `CHANGELOG.md`. `make versions` and `make release-plan` wrap it.
- **Roadmap.** Milestones are decoupled from versions. The roadmap's themes become milestones
  M1 to M5. Version 1.0.0 remains the stability commitment, released when its criteria are met.
- **Bootstrap.** This change bumps `VERSION` to 0.2.0 and dates the accumulated `[Unreleased]`
  entries as 0.2.0, so its merge publishes `v0.2.0`. Version 0.1.0 has no tag; it can be tagged
  by hand at the PR #1 merge commit.

This replaces the "Release process" section of CONTRIBUTING.md.

## Consequences

- Every merged change to fit has a version a user can name, a dated changelog section and,
  once released, a tag and a checksummed binary. A bug report can cite `fit --version`.
- Numerical changes are released promptly, and each is visible in the version number (MAJOR,
  or MINOR before 1.0), not hidden in an unreleased batch.
- Every pull request that changes code needs a bump and a changelog section. That is a small
  cost per change, and the gate catches it when it is forgotten.
- Two pull requests that both bump `VERSION` conflict. The second must rebase and bump again,
  which is the desired behaviour, since each merge is its own release.
- Version numbers advance faster than milestones. Older documents that say "v0.3" mean
  milestone M3; the roadmap gives the mapping. Accepted ADRs keep their wording.
- The released binary is built on Ubuntu 24.04 with GCC 13 and links the system C++ runtime
  dynamically. Other platforms build from source until there is a user for them (roadmap,
  packaging).
- Releases depend on GitHub Actions and the `gh` CLI. A failed run can be retried with
  `workflow_dispatch`.

## Alternatives considered

- **Keep milestone releases with a manual release PR.** This works, but it leaves merged work,
  and numerical changes in particular, without a version for weeks. It also relies on a person
  to tag and write notes. Rejected.
- **Derive the version from git tags** (`git describe`). This needs no file, but the version is
  then unknown in a source archive without history, and the gate cannot compare a branch with
  its base before a tag exists. Rejected in favour of an explicit `VERSION` file.
- **Keep the version in `CMakeLists.txt`.** CMake's `project(VERSION)` accepts no pre-release
  suffix, and tools would have to parse CMake. Rejected.
- **A bump bot or conventional-commit release tool** (release-please, semantic-release). It
  infers the bump from commit types, but the size of a bump in fit depends on numbers moving,
  which commit types do not capture. It also adds a third-party dependency to the process.
  Rejected; the author chooses and the reviewer checks.
- **Tag names per component** (alpha-tango's `<skill>-v<version>`). fit ships one product, so
  the conventional `v<version>` is used.

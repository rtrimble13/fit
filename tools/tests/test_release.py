"""Tests for tools/release.py (standard library unittest; run by ctest or directly with
`python3 -m unittest discover -s tools/tests`).

Most tests build a throwaway git repository, so they do not depend on the real VERSION or
CHANGELOG.md; RealRepository checks the real ones.
"""

from __future__ import annotations

import contextlib
import hashlib
import io
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO / "tools"))

import release  # noqa: E402
from release import Version  # noqa: E402

CHANGELOG = """# Changelog

## [Unreleased]

## [{version}] - 2026-01-01

### Added

- Something.

[Unreleased]: https://example.com/compare/v{version}...HEAD
"""


def write(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def git(root: Path, *args: str) -> str:
    return subprocess.run(
        ["git", "-c", "user.name=test", "-c", "user.email=test@example.com", "-c", "commit.gpgsign=false", *args],
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
    ).stdout


class TempRepo(unittest.TestCase):
    """A repository with VERSION 1.0.0, a matching changelog, a source file and a doc."""

    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix="fit-release-test-"))
        self.addCleanup(shutil.rmtree, self.root)
        write(self.root / "VERSION", "1.0.0\n")
        write(self.root / "CHANGELOG.md", CHANGELOG.format(version="1.0.0"))
        write(self.root / "LICENSE", "MIT\n")
        write(self.root / "src" / "a.cpp", "int a;\n")
        write(self.root / "docs" / "guide.md", "# Guide\n")

    def init_git(self):
        git(self.root, "init", "-q", "-b", "main")
        git(self.root, "add", "-A")
        git(self.root, "commit", "-q", "-m", "initial")
        git(self.root, "checkout", "-q", "-b", "feature")

    def bump(self, version: str, section: bool = True):
        write(self.root / "VERSION", version + "\n")
        if section:
            log = self.root / "CHANGELOG.md"
            text = log.read_text(encoding="utf-8")
            log.write_text(
                text.replace("## [Unreleased]\n", f"## [Unreleased]\n\n## [{version}] - 2026-02-01\n\n- More.\n"),
                encoding="utf-8",
            )

    def findings(self) -> release.Findings:
        _, findings = release.validate(self.root)
        return findings


# --------------------------------------------------------------------------- #
# Versions
# --------------------------------------------------------------------------- #


class Versions(unittest.TestCase):
    def test_semver_precedence(self):
        # The precedence example of Semantic Versioning 2.0.0, section 11.
        chain = [
            "1.0.0-alpha",
            "1.0.0-alpha.1",
            "1.0.0-alpha.beta",
            "1.0.0-beta",
            "1.0.0-beta.2",
            "1.0.0-beta.11",
            "1.0.0-rc.1",
            "1.0.0",
            "1.0.1",
            "1.1.0",
            "2.0.0",
        ]
        for lower, higher in zip(chain, chain[1:]):
            with self.subTest(lower=lower, higher=higher):
                self.assertLess(Version.parse(lower), Version.parse(higher))

    def test_build_metadata_is_ignored(self):
        self.assertEqual(Version.parse("1.2.3+build.5"), Version.parse("1.2.3"))

    def test_invalid_versions(self):
        for bad in ("1.0", "v1.0.0", "01.0.0", "1.0.0-", "1.0.0-01", ""):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                Version.parse(bad)

    def test_prerelease_and_tag(self):
        self.assertTrue(Version.parse("0.3.0-rc.1").is_prerelease)
        self.assertFalse(Version.parse("0.3.0").is_prerelease)
        self.assertEqual(str(Version.parse("0.3.0-rc.1")), "0.3.0-rc.1")
        self.assertEqual(release.tag_for("0.3.0"), "v0.3.0")


# --------------------------------------------------------------------------- #
# VERSION and the changelog
# --------------------------------------------------------------------------- #


class Changelog(TempRepo):
    def test_consistent_repository_validates(self):
        self.assertEqual(self.findings().errors, [])

    def test_latest_section_must_match_version(self):
        write(self.root / "VERSION", "1.1.0\n")
        messages = [f.message for f in self.findings().errors]
        self.assertTrue(any("latest section is [1.0.0] but VERSION is 1.1.0" in m for m in messages), messages)

    def test_order_dates_and_unreleased(self):
        write(
            self.root / "CHANGELOG.md",
            "## [1.0.0] - 2026-01-01\n\n## [Unreleased]\n\n## [1.1.0] - 2026-13-01\n\n## [0.9.0]\n",
        )
        messages = [f.message for f in self.findings().errors]
        self.assertTrue(any("[Unreleased] must come before" in m for m in messages), messages)
        self.assertTrue(any("newest first (1.1.0 after 1.0.0)" in m for m in messages), messages)
        self.assertTrue(any("invalid date '2026-13-01'" in m for m in messages), messages)
        self.assertTrue(any("section [0.9.0] needs a date" in m for m in messages), messages)

    def test_invalid_version_file(self):
        write(self.root / "VERSION", "1.0\n")
        self.assertTrue(any("not a semantic version" in f.message for f in self.findings().errors))
        (self.root / "VERSION").unlink()
        self.assertTrue(any("VERSION not found" in f.message for f in self.findings().errors))

    def test_no_version_sections(self):
        write(self.root / "CHANGELOG.md", "# Changelog\n\n## [Unreleased]\n")
        self.assertTrue(any("no version sections" in f.message for f in self.findings().errors))

    def test_section_extraction_drops_link_definitions(self):
        text = (self.root / "CHANGELOG.md").read_text(encoding="utf-8")
        self.assertEqual(release.changelog_section(text, "1.0.0"), "### Added\n\n- Something.")
        self.assertIsNone(release.changelog_section(text, "9.9.9"))


# --------------------------------------------------------------------------- #
# The version gate
# --------------------------------------------------------------------------- #


@unittest.skipIf(shutil.which("git") is None, "git not installed")
class VersionGate(TempRepo):
    def gate(self):
        return release.check_versions(self.root, "main")

    def test_shipped_change_requires_bump_and_section(self):
        self.init_git()
        write(self.root / "src" / "a.cpp", "int a = 1;\n")
        report, findings = self.gate()
        self.assertTrue(any("bump it" in f.message for f in findings.errors), findings.errors)

        self.bump("1.1.0", section=False)
        _, findings = self.gate()
        self.assertTrue(any("add a '## [1.1.0]" in f.message for f in findings.errors), findings.errors)

        self.bump("1.1.0")
        report, findings = self.gate()
        self.assertEqual(findings.errors, [])
        self.assertEqual(report, ["fit: 1.0.0 -> 1.1.0"])

    def test_changes_that_ship_nothing_need_no_bump(self):
        self.init_git()
        write(self.root / "docs" / "guide.md", "# Guide\n\nMore.\n")
        write(self.root / "tests" / "new_test.cpp", "// test\n")
        write(self.root / "CHANGELOG.md", (self.root / "CHANGELOG.md").read_text() + "\n")
        report, findings = self.gate()
        self.assertEqual((report, findings.errors), ([], []))

    def test_build_files_and_untracked_sources_are_shipped(self):
        self.init_git()
        write(self.root / "include" / "fit" / "new.hpp", "#pragma once\n")  # untracked
        self.assertTrue(any("bump it" in f.message for f in self.gate()[1].errors))
        for path in ("CMakeLists.txt", "CMakePresets.json", "src/x.cpp", "VERSION"):
            self.assertTrue(release.is_shipped(path), path)
        for path in ("tests/CMakeLists.txt", "docs/ROADMAP.md", "tools/release.py", "README.md"):
            self.assertFalse(release.is_shipped(path), path)

    def test_version_must_increase(self):
        self.init_git()
        self.bump("0.9.0")
        _, findings = self.gate()
        self.assertTrue(any("VERSION is 0.9.0 (base 1.0.0)" in f.message for f in findings.errors))

    def test_prerelease_then_release(self):
        self.init_git()
        self.bump("1.1.0-rc.1")
        report, findings = self.gate()
        self.assertEqual((report, findings.errors), (["fit: 1.0.0 -> 1.1.0-rc.1"], []))
        git(self.root, "add", "-A")
        git(self.root, "commit", "-q", "-m", "rc")
        git(self.root, "checkout", "-q", "main")
        git(self.root, "merge", "-q", "--ff-only", "feature")
        git(self.root, "checkout", "-q", "-b", "final")
        self.bump("1.1.0")
        report, findings = self.gate()
        self.assertEqual((report, findings.errors), (["fit: 1.1.0-rc.1 -> 1.1.0"], []))

    def test_base_without_version_file_uses_cmakelists(self):
        # Before VERSION existed the version was declared in CMakeLists.txt.
        (self.root / "VERSION").unlink()
        write(self.root / "CMakeLists.txt", 'project(fit VERSION 1.0.0 DESCRIPTION "x" LANGUAGES CXX)\n')
        self.init_git()
        self.bump("1.0.0", section=False)  # VERSION appears, unchanged in value
        _, findings = self.gate()
        self.assertTrue(any("VERSION is 1.0.0 (base 1.0.0)" in f.message for f in findings.errors))
        self.bump("1.1.0")
        report, findings = self.gate()
        self.assertEqual((report, findings.errors), (["fit: 1.0.0 -> 1.1.0"], []))


# --------------------------------------------------------------------------- #
# Packaging and releases
# --------------------------------------------------------------------------- #


class Packaging(TempRepo):
    def binary(self) -> Path:
        path = write(self.root / "build" / "fit", "#!/bin/sh\necho fit\n")
        path.chmod(0o755)
        return path

    def test_archive_contents_and_determinism(self):
        first = release.package(self.root, self.binary(), "1.0.0", self.root / "a", "linux-x86_64")
        second = release.package(self.root, self.binary(), "1.0.0", self.root / "b", "linux-x86_64")
        self.assertEqual(first.name, "fit-1.0.0-linux-x86_64.tar.gz")
        self.assertEqual(first.read_bytes(), second.read_bytes())
        with tarfile.open(first) as tar:
            members = {m.name: m for m in tar.getmembers()}
        self.assertEqual(
            sorted(members), ["fit-1.0.0", "fit-1.0.0/LICENSE", "fit-1.0.0/bin", "fit-1.0.0/bin/fit"]
        )
        self.assertEqual(members["fit-1.0.0/bin/fit"].mode, 0o755)
        self.assertEqual(members["fit-1.0.0/LICENSE"].mode, 0o644)
        self.assertTrue(all(m.mtime == 0 and m.uid == 0 for m in members.values()))
        digest = hashlib.sha256(first.read_bytes()).hexdigest()
        checksum = (first.parent / (first.name + ".sha256")).read_text()
        self.assertEqual(checksum, f"{digest}  {first.name}\n")

    def test_missing_binary(self):
        with self.assertRaises(FileNotFoundError):
            release.package(self.root, self.root / "nope", "1.0.0", self.root / "dist")

    @unittest.skipIf(shutil.which("git") is None, "git not installed")
    def test_release_dry_run_and_idempotence(self):
        self.init_git()
        self.bump("1.1.0-rc.1")
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = release.release(self.root, self.binary(), self.root / "dist", True, "linux-x86_64")
        self.assertEqual(code, 0)
        text = out.getvalue()
        self.assertIn("would release v1.1.0-rc.1", text)
        self.assertIn("--prerelease", text)
        self.assertIn("fit-1.1.0-rc.1-linux-x86_64.tar.gz", text)
        self.assertIn("- More.", text)

        git(self.root, "tag", "v1.1.0-rc.1")
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(release.release(self.root, self.binary(), self.root / "dist", True), 0)
        self.assertIn("v1.1.0-rc.1 is already released", out.getvalue())

    @unittest.skipIf(shutil.which("git") is None, "git not installed")
    def test_release_refuses_an_inconsistent_changelog(self):
        self.init_git()
        self.bump("1.1.0", section=False)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = release.release(self.root, self.binary(), self.root / "dist", True)
        self.assertEqual(code, 1)
        self.assertIn("refusing to release", out.getvalue())


class RealRepository(unittest.TestCase):
    def test_version_and_changelog_are_consistent(self):
        version, findings = release.validate(REPO)
        self.assertEqual(findings.errors, [], [f"{f.path}:{f.line}: {f.message}" for f in findings.errors])
        self.assertIsNotNone(release.changelog_section((REPO / "CHANGELOG.md").read_text(), version))

    def test_github_annotations(self):
        findings = release.Findings()
        findings.error("VERSION", "bad", 1)
        out = io.StringIO()
        old = os.environ.get("GITHUB_ACTIONS")
        os.environ["GITHUB_ACTIONS"] = "true"
        try:
            findings.emit(out)
        finally:
            if old is None:
                del os.environ["GITHUB_ACTIONS"]
            else:
                os.environ["GITHUB_ACTIONS"] = old
        self.assertEqual(out.getvalue(), "::error file=VERSION,line=1::bad\n")


if __name__ == "__main__":
    unittest.main()

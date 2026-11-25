#!/usr/bin/env python3
"""release: versioning and release tool for fit.

Subcommands:
  version         print the version held in VERSION
  validate        check VERSION and CHANGELOG.md
  check-versions  require a version bump and a changelog section when shipped files change
  package         build the release archive and its SHA-256 checksum
  release         publish a GitHub release for VERSION unless its tag exists
  notes           print the changelog section of a version

Run `python3 tools/release.py <subcommand> --help` for options. Standard library only
(Python 3.10+); `release` also needs the GitHub CLI (`gh`) and GH_TOKEN. The model is
described in docs/versioning-and-releases.md.
"""

from __future__ import annotations

import argparse
import datetime as dt
import functools
import gzip
import hashlib
import io
import os
import platform
import re
import shlex
import subprocess
import sys
import tarfile
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

DEFAULT_ROOT = Path(__file__).resolve().parent.parent
VERSION_FILE = "VERSION"
CHANGELOG = "CHANGELOG.md"
TAG_PREFIX = "v"

# Paths whose change alters what a release ships, so a pull request touching any of them must
# bump VERSION. Everything else (tests, docs, examples, tools, CI) ships nothing.
SHIPPED_PREFIXES = ("src/", "include/")
SHIPPED_FILES = frozenset({"CMakeLists.txt", "CMakePresets.json", VERSION_FILE})

# Files packed beside the binary in a release archive.
ARCHIVE_EXTRAS = ("LICENSE",)

SEMVER_RE = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    r"(?:-((?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)"
    r"(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?"
    r"(?:\+([0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?$"
)
CHANGELOG_HEADING_RE = re.compile(
    r"^## \[(?P<version>[^\]]+)\](?:[ \t]+-[ \t]+(?P<date>\S+))?[ \t]*$", re.MULTILINE
)
REF_DEF_RE = re.compile(r"^ {0,3}\[[^\]]+\]:\s*\S+")
# The version before VERSION existed lived in CMakeLists.txt.
CMAKE_VERSION_RE = re.compile(r"project\(\s*fit\s+VERSION\s+([0-9][0-9A-Za-z.+-]*)", re.I)


# --------------------------------------------------------------------------- #
# Versions
# --------------------------------------------------------------------------- #


@functools.total_ordering
@dataclass(frozen=True)
class Version:
    """A Semantic Versioning 2.0.0 version (build metadata is ignored)."""

    major: int
    minor: int
    patch: int
    pre: tuple = ()

    @classmethod
    def parse(cls, text: str) -> Version:
        match = SEMVER_RE.match(text or "")
        if not match:
            raise ValueError(f"not a semantic version (MAJOR.MINOR.PATCH[-PRERELEASE]): {text!r}")
        pre = ()
        if match.group(4):
            pre = tuple(int(p) if p.isdigit() else p for p in match.group(4).split("."))
        return cls(int(match.group(1)), int(match.group(2)), int(match.group(3)), pre)

    @property
    def is_prerelease(self) -> bool:
        return bool(self.pre)

    def _key(self) -> tuple:
        # A release sorts above its pre-releases; numeric identifiers sort below alphanumeric
        # ones; a longer identifier list wins when the shared prefix ties (SemVer 2.0.0, 11).
        pre = tuple((0, p, "") if isinstance(p, int) else (1, 0, p) for p in self.pre)
        return (self.major, self.minor, self.patch, 0 if self.pre else 1, pre)

    def __lt__(self, other: Version) -> bool:
        return self._key() < other._key()

    def __str__(self) -> str:
        text = f"{self.major}.{self.minor}.{self.patch}"
        if self.pre:
            text += "-" + ".".join(str(p) for p in self.pre)
        return text


def tag_for(version: str) -> str:
    return TAG_PREFIX + version


# --------------------------------------------------------------------------- #
# Findings
# --------------------------------------------------------------------------- #


@dataclass
class Finding:
    level: str
    path: str
    message: str
    line: int | None = None


@dataclass
class Findings:
    items: list[Finding] = field(default_factory=list)

    def error(self, path: str, message: str, line: int | None = None) -> None:
        self.items.append(Finding("error", path, message, line))

    def warning(self, path: str, message: str, line: int | None = None) -> None:
        self.items.append(Finding("warning", path, message, line))

    @property
    def errors(self) -> list[Finding]:
        return [f for f in self.items if f.level == "error"]

    def emit(self, stream=None) -> None:
        """Print findings; inside GitHub Actions also as annotations on the file and line."""
        stream = stream or sys.stdout
        actions = os.environ.get("GITHUB_ACTIONS") == "true"
        for f in self.items:
            where = f"{f.path}:{f.line}" if f.line else f.path
            if actions:
                location = f"file={f.path}" + (f",line={f.line}" if f.line else "")
                print(f"::{f.level} {location}::{f.message}", file=stream)
            else:
                print(f"{f.level}: {where}: {f.message}", file=stream)


# --------------------------------------------------------------------------- #
# VERSION and CHANGELOG.md
# --------------------------------------------------------------------------- #


def read_version(root: Path) -> str:
    path = root / VERSION_FILE
    if not path.is_file():
        raise FileNotFoundError(f"{VERSION_FILE} not found in {root}")
    return path.read_text(encoding="utf-8").strip()


def changelog_headings(text: str) -> list[tuple[str, str | None, int]]:
    return [
        (m.group("version"), m.group("date"), text.count("\n", 0, m.start()) + 1)
        for m in CHANGELOG_HEADING_RE.finditer(text)
    ]


def changelog_section(text: str, version: str) -> str | None:
    """The body of the `## [version]` section, without trailing link definitions."""
    headings = list(CHANGELOG_HEADING_RE.finditer(text))
    for i, match in enumerate(headings):
        if match.group("version") != version:
            continue
        end = headings[i + 1].start() if i + 1 < len(headings) else len(text)
        lines = text[match.end() : end].strip("\n").split("\n")
        while lines and (not lines[-1].strip() or REF_DEF_RE.match(lines[-1])):
            lines.pop()
        return "\n".join(lines).strip()
    return None


def check_changelog(text: str, version: str | None, findings: Findings, where: str = CHANGELOG):
    """Keep a Changelog structure, newest version first and matching VERSION."""
    seen_release = False
    released: list[tuple[Version, int]] = []
    for heading, date, line in changelog_headings(text):
        if heading.lower() == "unreleased":
            if seen_release:
                findings.error(where, "[Unreleased] must come before every version section", line)
            continue
        seen_release = True
        try:
            parsed = Version.parse(heading)
        except ValueError as exc:
            findings.error(where, str(exc), line)
            continue
        if not date:
            findings.error(where, f"section [{heading}] needs a date: '## [{heading}] - YYYY-MM-DD'", line)
        else:
            try:
                dt.date.fromisoformat(date)
            except ValueError:
                findings.error(where, f"section [{heading}] has an invalid date {date!r} (YYYY-MM-DD)", line)
        released.append((parsed, line))
    for (newer, _), (older, line) in zip(released, released[1:]):
        if not older < newer:
            findings.error(where, f"versions must be listed newest first ({older} after {newer})", line)
    if not released:
        findings.error(where, "no version sections; add '## [x.y.z] - YYYY-MM-DD'")
        return
    if version and SEMVER_RE.match(version):
        latest = released[0][0]
        if latest != Version.parse(version):
            findings.error(
                where,
                f"latest section is [{latest}] but {VERSION_FILE} is {version}; "
                f"add '## [{version}] - YYYY-MM-DD' describing the change",
                released[0][1],
            )


def validate(root: Path) -> tuple[str | None, Findings]:
    findings = Findings()
    try:
        version = read_version(root)
    except FileNotFoundError as exc:
        findings.error(VERSION_FILE, str(exc))
        version = None
    else:
        try:
            Version.parse(version)
        except ValueError as exc:
            findings.error(VERSION_FILE, str(exc), 1)
    log = root / CHANGELOG
    if not log.is_file():
        findings.error(CHANGELOG, "missing")
    else:
        check_changelog(log.read_text(encoding="utf-8"), version, findings)
    return version, findings


# --------------------------------------------------------------------------- #
# The version gate
# --------------------------------------------------------------------------- #


class GitError(RuntimeError):
    pass


def git(root: Path, *args: str) -> str:
    result = subprocess.run(["git", *args], cwd=root, capture_output=True, text=True)
    if result.returncode != 0:
        raise GitError(f"git {' '.join(args)}: {result.stderr.strip() or 'failed'}")
    return result.stdout


def is_shipped(path: str) -> bool:
    return path in SHIPPED_FILES or path.startswith(SHIPPED_PREFIXES)


def changed_files(root: Path, base: str) -> tuple[str, list[str]]:
    """Files changed since the merge base, including uncommitted and untracked ones."""
    merge_base = git(root, "merge-base", base, "HEAD").strip()
    tracked = git(root, "diff", "--name-only", merge_base).split("\n")
    untracked = git(root, "ls-files", "--others", "--exclude-standard").split("\n")
    return merge_base, sorted({f for f in tracked + untracked if f})


def base_version(root: Path, commit: str) -> str | None:
    """VERSION at `commit`, or the version CMakeLists.txt declared before VERSION existed."""
    try:
        return git(root, "show", f"{commit}:{VERSION_FILE}").strip()
    except GitError:
        pass
    try:
        match = CMAKE_VERSION_RE.search(git(root, "show", f"{commit}:CMakeLists.txt"))
    except GitError:
        return None
    return match.group(1) if match else None


def check_versions(root: Path, base: str) -> tuple[list[str], Findings]:
    findings = Findings()
    merge_base, changed = changed_files(root, base)
    shipped = [f for f in changed if is_shipped(f)]
    if not shipped:
        return [], findings
    report: list[str] = []
    try:
        head_text = read_version(root)
        head = Version.parse(head_text)
    except (FileNotFoundError, ValueError) as exc:
        findings.error(VERSION_FILE, str(exc))
        return report, findings
    previous_text = base_version(root, merge_base)
    if previous_text is None:
        report.append(f"fit: first version {head}")
    else:
        try:
            previous = Version.parse(previous_text)
        except ValueError:
            findings.warning(VERSION_FILE, f"base version {previous_text!r} is not semver; skipping the bump check")
        else:
            if head > previous:
                report.append(f"fit: {previous} -> {head}")
            else:
                findings.error(
                    VERSION_FILE,
                    f"{len(shipped)} shipped file(s) changed ({', '.join(shipped[:3])}"
                    f"{', ...' if len(shipped) > 3 else ''}) but {VERSION_FILE} is {head} (base "
                    f"{previous}); bump it per docs/versioning-and-releases.md",
                    1,
                )
                report.append(f"fit: {previous} -> {head} (needs a bump)")
    log = root / CHANGELOG
    if not log.is_file() or changelog_section(log.read_text(encoding="utf-8"), str(head)) is None:
        findings.error(CHANGELOG, f"add a '## [{head}] - YYYY-MM-DD' section for this change")
    return report, findings


# --------------------------------------------------------------------------- #
# Packaging and releases
# --------------------------------------------------------------------------- #


def platform_name() -> str:
    machine = {"amd64": "x86_64", "arm64": "aarch64"}.get(platform.machine().lower(), platform.machine().lower())
    return f"{platform.system().lower()}-{machine}"


def archive_name(version: str, plat: str) -> str:
    return f"fit-{version}-{plat}.tar.gz"


def package(root: Path, binary: Path, version: str, out_dir: Path, plat: str | None = None) -> Path:
    """Write a deterministic fit-<version>-<platform>.tar.gz (plus .sha256).

    The archive holds fit-<version>/bin/fit and the files in ARCHIVE_EXTRAS, with fixed
    timestamps, owners and order, so the same inputs always give the same bytes.
    """
    if not binary.is_file():
        raise FileNotFoundError(f"binary not found: {binary} (build the release preset first)")
    out_dir.mkdir(parents=True, exist_ok=True)
    archive = out_dir / archive_name(version, plat or platform_name())
    top = f"fit-{version}"
    members = [(f"{top}/bin/fit", binary, 0o755)]
    members += [(f"{top}/{name}", root / name, 0o644) for name in ARCHIVE_EXTRAS]
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w", format=tarfile.PAX_FORMAT) as tar:
        for directory in (top, f"{top}/bin"):
            info = tarfile.TarInfo(directory)
            info.type, info.mode, info.mtime = tarfile.DIRTYPE, 0o755, 0
            tar.addfile(info)
        for name, path, mode in members:
            data = path.read_bytes()
            info = tarfile.TarInfo(name)
            info.size, info.mode, info.mtime = len(data), mode, 0
            tar.addfile(info, io.BytesIO(data))
    with open(archive, "wb") as fh, gzip.GzipFile(filename="", mode="wb", fileobj=fh, mtime=0) as gz:
        gz.write(buffer.getvalue())
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (out_dir / f"{archive.name}.sha256").write_text(f"{digest}  {archive.name}\n", encoding="utf-8")
    return archive


def is_released(root: Path, version: str) -> bool:
    return bool(git(root, "tag", "--list", tag_for(version)).strip())


def release_notes(root: Path, version: str, archive: str, target: str) -> str:
    notes = changelog_section((root / CHANGELOG).read_text(encoding="utf-8"), version) or "See CHANGELOG.md."
    repository = os.environ.get("GITHUB_REPOSITORY")
    server = os.environ.get("GITHUB_SERVER_URL", "https://github.com")
    readme = f"{server}/{repository}/blob/{target}/README.md" if repository else "README.md"
    return notes + (
        f"\n\n---\n\n`{archive}` holds the `fit` binary built by the release workflow, with the "
        f"licence. Check it with `sha256sum -c {archive}.sha256`. To build from source, see the "
        f"[README]({readme}#build-and-install)."
    )


def release(root: Path, binary: Path, out_dir: Path, dry_run: bool, plat: str | None = None) -> int:
    version, findings = validate(root)
    if findings.errors or version is None:
        findings.emit()
        print("refusing to release with validation errors")
        return 1
    tag = tag_for(version)
    if is_released(root, version):
        print(f"{tag} is already released; nothing to do")
        return 0
    target = os.environ.get("GITHUB_SHA") or git(root, "rev-parse", "HEAD").strip()
    archive = package(root, binary, version, out_dir, plat)
    notes = release_notes(root, version, archive.name, target)
    command = [
        "gh",
        "release",
        "create",
        tag,
        str(archive),
        f"{archive}.sha256",
        "--title",
        f"fit v{version}",
        "--target",
        target,
    ]
    if Version.parse(version).is_prerelease:
        command.append("--prerelease")
    if dry_run:
        print(f"would release {tag} from {target[:12]}:\n  {shlex.join(command)}")
        print("  notes:\n    " + notes.replace("\n", "\n    "))
        return 0
    with tempfile.NamedTemporaryFile("w", suffix=".md", delete=False, encoding="utf-8") as fh:
        fh.write(notes)
    try:
        subprocess.run([*command, "--notes-file", fh.name], check=True)
    finally:
        os.unlink(fh.name)
    print(f"released {tag}")
    return 0


# --------------------------------------------------------------------------- #
# Command line
# --------------------------------------------------------------------------- #


def cmd_version(args) -> int:
    print(read_version(args.root))
    return 0


def cmd_validate(args) -> int:
    version, findings = validate(args.root)
    findings.emit()
    if not findings.errors:
        print(f"VERSION {version} and CHANGELOG.md are consistent")
    return 1 if findings.errors else 0


def cmd_check_versions(args) -> int:
    try:
        report, findings = check_versions(args.root, args.base)
    except GitError as exc:
        print(f"error: {exc}\nFetch the base branch with full history (actions/checkout fetch-depth: 0).")
        return 1
    for line in report or ["no shipped files changed; no version bump needed"]:
        print(line)
    findings.emit()
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary and report:
        with open(summary, "a", encoding="utf-8") as fh:
            fh.write("### Version\n\n" + "\n".join(f"- {line}" for line in report) + "\n")
    return 1 if findings.errors else 0


def cmd_package(args) -> int:
    version, findings = validate(args.root)
    if findings.errors or version is None:
        findings.emit()
        print("fix the errors above before packaging")
        return 1
    archive = package(args.root, args.binary, version, args.out, args.platform)
    print(archive)
    return 0


def cmd_release(args) -> int:
    return release(args.root, args.binary, args.out, args.dry_run, args.platform)


def cmd_notes(args) -> int:
    version = args.version or read_version(args.root)
    section = changelog_section((args.root / CHANGELOG).read_text(encoding="utf-8"), version)
    if section is None:
        print(f"no changelog section for {version}", file=sys.stderr)
        return 1
    print(section)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="release.py", description=__doc__.split("\n")[0])
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT, help="repository root")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("version", help="print the version in VERSION")
    p.set_defaults(func=cmd_version)
    p = sub.add_parser("validate", help="check VERSION and CHANGELOG.md")
    p.set_defaults(func=cmd_validate)
    p = sub.add_parser("check-versions", help="require a version bump when shipped files change")
    p.add_argument("--base", default="origin/main", help="branch or commit to compare with")
    p.set_defaults(func=cmd_check_versions)
    for name, func, text in (
        ("package", cmd_package, "build the release archive and checksum"),
        ("release", cmd_release, "publish a GitHub release for VERSION unless its tag exists"),
    ):
        p = sub.add_parser(name, help=text)
        p.add_argument("--binary", type=Path, default=Path("build/release/fit"), help="built fit binary")
        p.add_argument("--out", type=Path, default=Path("dist"), help="output directory")
        p.add_argument("--platform", help="platform part of the archive name (default: this machine)")
        if name == "release":
            p.add_argument("--dry-run", action="store_true", help="show what would be released")
        p.set_defaults(func=func)
    p = sub.add_parser("notes", help="print a version's changelog section")
    p.add_argument("--version", help="version (default: VERSION)")
    p.set_defaults(func=cmd_notes)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    args.root = args.root.resolve()
    for attr in ("binary", "out"):
        value = getattr(args, attr, None)
        if value is not None and not value.is_absolute():
            setattr(args, attr, args.root / value)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())

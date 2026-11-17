---
name: Bug report
about: A wrong number, a crash, or behaviour that contradicts the documentation
title: "bug: "
labels: bug
---

## Summary

<!-- One or two sentences. -->

## Category

- [ ] Wrong numerical result
- [ ] Crash / sanitizer report
- [ ] Incorrect error handling, message or exit code
- [ ] Input parsing
- [ ] Output formatting
- [ ] Documentation
- [ ] Build / tooling

## Reproduction

Exact command line:

```sh
fit ...
```

Input files (attach, or paste a minimal version — remove anything confidential):

```csv
```

## Actual result

<!-- Full stdout and stderr, and the exit code (`echo $?`). -->

```text
```

## Expected result

<!-- For a numerical issue, give the expected value AND where it comes from: another system
     (name and version), a published reference, or a hand calculation. State every convention
     assumed: day count, frequency, compounding, settlement date, calendar, interpolation. -->

## Size of discrepancy

<!-- e.g. "clean price differs by 0.0031 per 100", "DV01 differs by 2%". -->

## Environment

- `fit` version (`fit --version`) or commit:
- Operating system:
- Compiler and version:
- Build preset (`dev` / `release`):

## Additional context

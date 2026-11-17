# 0002. C++20, CMake and Ninja toolchain

- **Status:** Accepted
- **Date:** 2026-11-17

## Context

The tool is used interactively on a desk and in batch from scripts. It must start instantly,
price a portfolio in well under a second, be deployable as a single binary onto locked-down
hosts, and be buildable reproducibly by any contributor and by CI. The core library should also
be embeddable later in other processes and language bindings.

## Decision

We will implement `fit` in ISO C++20 with compiler extensions disabled.

- **Build system:** CMake 3.24 or newer with the Ninja generator. All configuration goes
  through `CMakePresets.json`; contributors and CI use the same presets.
  - `dev` — Debug, AddressSanitizer and UndefinedBehaviorSanitizer enabled, build directory
    `build/dev`. This is the default for development and for CI test runs.
  - `release` — optimised, no sanitizers, build directory `build/release`.
- **Supported compilers:** GCC 13 or newer and Clang 17 or newer, on Linux. Both are built and
  tested in CI.
- **Warnings:** `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`, treated as errors.
- **Targets:** a static library `fit_core` and an executable `fit` that links it.
- **Tooling:** `clang-format` for layout and `clang-tidy` for static analysis, configured at the
  repository root.

The canonical workflow is:

```sh
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
```

## Consequences

- A statically linked native binary has no interpreter or runtime to deploy and negligible
  start-up cost.
- C++20 gives `std::span`, concepts, `<numbers>`, designated initialisers and three-way
  comparison, which keep numerical code direct without external libraries.
- `-Wconversion` as an error forces every narrowing conversion in numerical code to be explicit.
  This costs some `static_cast` noise and is accepted.
- Sanitizers in the default test configuration catch out-of-range interpolation lookups and
  similar faults early; the `dev` build is noticeably slower than `release`.
- Windows and macOS are not supported targets in v0.1. Nothing deliberately prevents them, but
  they are not built or tested.
- We do not use C++20 modules or `std::format`/`std::chrono` calendar features, whose support is
  uneven across the supported compilers.

## Alternatives considered

- **Python (with NumPy/QuantLib).** Fast to write but slow to start, heavy to deploy and
  dependent on a third-party pricing library whose conventions we would then have to adopt
  wholesale. Python bindings over `fit_core` are on the roadmap instead.
- **Rust.** Technically suitable. Rejected because C++ is the prevailing language of the desk
  quant libraries this tool must sit alongside and eventually be embedded in.
- **C++17.** Would widen compiler support slightly but give up language features we use; the
  target hosts already have GCC 13.
- **C++23.** Library support is still incomplete on the supported compilers.
- **Bazel or Meson.** Capable, but CMake is the common denominator for C++ consumers and IDEs.

#pragma once

// The build defines FIT_VERSION_STRING from the VERSION file at the repository root, the single
// source of the version (CMakeLists.txt, docs/versioning-and-releases.md).
#ifndef FIT_VERSION_STRING
#error "FIT_VERSION_STRING is not defined: build fit with CMake, which reads it from VERSION"
#endif

namespace fit {

inline constexpr const char* kVersion = FIT_VERSION_STRING;

} // namespace fit

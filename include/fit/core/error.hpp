#pragma once

#include <stdexcept>
#include <string>

namespace fit {

/// Base class for every error raised by the library.
///
/// Errors carry a human-readable message that names the offending input. They are thrown by
/// the library and caught only at the CLI boundary (see ADR-0011).
class Error : public std::runtime_error {
public:
    explicit Error(const std::string& message) : std::runtime_error(message) {}
};

/// Raised for malformed command lines; mapped to exit code 2 by the CLI.
class UsageError : public Error {
public:
    explicit UsageError(const std::string& message) : Error(message) {}
};

/// Throws `Error(message)` when `condition` is false. A message built by concatenation is built
/// before the check, so code that runs very often tests the condition itself instead.
inline void require(bool condition, const std::string& message) {
    if (!condition) {
        throw Error(message);
    }
}

/// The same for a literal message, which is only turned into a string when the check fails.
inline void require(bool condition, const char* message) {
    if (!condition) {
        throw Error(message);
    }
}

} // namespace fit

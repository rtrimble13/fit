#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fit::cli {

/// Declaration of one command-line option.
struct OptionSpec {
    std::string name;       ///< without leading dashes, e.g. "asof"
    std::string value_name; ///< placeholder shown in help; empty for boolean flags
    std::string help;
    std::string default_value; ///< shown in help and returned when the option is absent
    bool required = false;
};

/// Parsed options of one command invocation.
class Args {
public:
    /// Parses `tokens` against `specs`. Accepts `--name value` and `--name=value`.
    /// Throws `UsageError` for unknown options, missing values and missing required options.
    Args(const std::vector<OptionSpec>& specs, const std::vector<std::string>& tokens);

    /// True when the option was given on the command line.
    bool has(const std::string& name) const { return values_.contains(name); }
    /// The option value or its default; nullopt when absent with no default.
    std::optional<std::string> get(const std::string& name) const;
    /// The option value or its default; throws `UsageError` when neither exists.
    std::string require(const std::string& name) const;

private:
    std::map<std::string, std::string> values_;
    std::map<std::string, std::string> defaults_;
};

} // namespace fit::cli

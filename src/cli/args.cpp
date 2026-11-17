#include "args.hpp"

#include "fit/core/error.hpp"

#include <algorithm>

namespace fit::cli {

Args::Args(const std::vector<OptionSpec>& specs, const std::vector<std::string>& tokens) {
    const auto find = [&](const std::string& name) {
        return std::find_if(specs.begin(), specs.end(),
                            [&](const OptionSpec& s) { return s.name == name; });
    };
    for (const OptionSpec& spec : specs) {
        if (!spec.default_value.empty()) {
            defaults_[spec.name] = spec.default_value;
        }
    }
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string& token = tokens[i];
        if (!token.starts_with("--") || token.size() == 2) {
            throw UsageError("unexpected argument '" + token + "'");
        }
        const std::size_t eq = token.find('=');
        const std::string name = token.substr(2, eq == std::string::npos ? eq : eq - 2);
        const auto spec = find(name);
        if (spec == specs.end()) {
            throw UsageError("unknown option '--" + name + "'");
        }
        if (values_.contains(name)) {
            throw UsageError("option '--" + name + "' given more than once");
        }
        if (spec->value_name.empty()) {
            if (eq != std::string::npos) {
                throw UsageError("option '--" + name + "' does not take a value");
            }
            values_[name] = "true";
        } else if (eq != std::string::npos) {
            values_[name] = token.substr(eq + 1);
        } else {
            if (i + 1 >= tokens.size()) {
                throw UsageError("option '--" + name + "' requires a value");
            }
            values_[name] = tokens[++i];
        }
    }
    for (const OptionSpec& spec : specs) {
        if (spec.required && !values_.contains(spec.name)) {
            throw UsageError("missing required option '--" + spec.name + "'");
        }
    }
}

std::optional<std::string> Args::get(const std::string& name) const {
    if (const auto it = values_.find(name); it != values_.end()) {
        return it->second;
    }
    if (const auto it = defaults_.find(name); it != defaults_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::string Args::require(const std::string& name) const {
    auto value = get(name);
    if (!value) {
        throw UsageError("missing required option '--" + name + "'");
    }
    return *value;
}

} // namespace fit::cli

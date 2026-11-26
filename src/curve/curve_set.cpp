#include "fit/curve/curve_set.hpp"

#include "fit/core/error.hpp"
#include "fit/io/csv.hpp"

#include <algorithm>
#include <filesystem>
#include <utility>

namespace fit {

namespace {

bool discounts_itself(const CurveSpec& spec) {
    return spec.discount.empty() || spec.discount == spec.name;
}

std::string names_of(const std::vector<CurveSpec>& specs) {
    std::string out;
    for (const CurveSpec& s : specs) {
        out += (out.empty() ? "" : ", ") + s.name;
    }
    return out;
}

} // namespace

std::vector<NamedCurve> build_curve_set(Date as_of, const std::vector<CurveSpec>& specs) {
    require(!specs.empty(), "curve set: no curves");
    for (std::size_t i = 0; i < specs.size(); ++i) {
        require(!specs[i].name.empty(), "curve set: a curve has an empty name");
        for (std::size_t j = 0; j < i; ++j) {
            require(specs[i].name != specs[j].name,
                    "curve set: curve '" + specs[i].name + "' is defined twice");
        }
    }
    const auto find = [&](const std::string& name) {
        return std::find_if(specs.begin(), specs.end(),
                            [&](const CurveSpec& s) { return s.name == name; });
    };
    for (const CurveSpec& s : specs) {
        require(discounts_itself(s) || find(s.discount) != specs.end(),
                "curve set: curve '" + s.name + "' is discounted on unknown curve '" + s.discount +
                    "' (curves: " + names_of(specs) + ")");
        if (s.reference.empty()) {
            continue;
        }
        require(s.reference != s.name, "curve set: curve '" + s.name +
                                           "' cannot be the reference curve of its own basis "
                                           "quotes");
        require(find(s.reference) != specs.end(),
                "curve set: curve '" + s.name + "' names unknown reference curve '" + s.reference +
                    "' (curves: " + names_of(specs) + ")");
        require(std::any_of(s.quotes.begin(), s.quotes.end(),
                            [](const Quote& q) { return q.type == QuoteType::Basis; }),
                "curve set: curve '" + s.name + "' names reference curve '" + s.reference +
                    "' but has no basis quotes");
    }

    // Repeatedly build the first curve, in the given order, whose discount and reference curves
    // are built.
    std::vector<NamedCurve> built;
    std::vector<bool> done(specs.size(), false);
    const auto built_curve = [&](const std::string& name) -> const NamedCurve* {
        const auto it = std::find_if(built.begin(), built.end(),
                                     [&](const NamedCurve& c) { return c.name == name; });
        return it == built.end() ? nullptr : &*it;
    };
    while (built.size() < specs.size()) {
        bool progressed = false;
        for (std::size_t i = 0; i < specs.size() && !progressed; ++i) {
            const CurveSpec& s = specs[i];
            const NamedCurve* discount = discounts_itself(s) ? nullptr : built_curve(s.discount);
            const NamedCurve* reference = s.reference.empty() ? nullptr : built_curve(s.reference);
            if (done[i] || (!discounts_itself(s) && discount == nullptr) ||
                (!s.reference.empty() && reference == nullptr)) {
                continue;
            }
            BootstrapOptions options = s.options;
            options.discount = discount != nullptr ? discount->bootstrap.curve : nullptr;
            options.reference = reference != nullptr ? reference->bootstrap.curve : nullptr;
            try {
                built.push_back({s.name, discounts_itself(s) ? s.name : s.discount, s.calendar,
                                 bootstrap(as_of, s.quotes, s.calendar, options)});
            } catch (const Error& e) {
                throw Error(specs.size() > 1 ? "curve '" + s.name + "': " + e.what() : e.what());
            }
            done[i] = true;
            progressed = true;
        }
        if (!progressed) {
            std::string cycle;
            for (std::size_t i = 0; i < specs.size(); ++i) {
                if (!done[i]) {
                    cycle += (cycle.empty() ? "" : ", ") + specs[i].name;
                }
            }
            throw Error("curve set: curves " + cycle +
                        " depend on each other in a cycle (through their discount or reference "
                        "curves)");
        }
    }
    return built;
}

std::vector<CurveSpec> load_curve_set(const std::string& path, Date as_of,
                                      const CurveSetDefaults& defaults) {
    const CsvTable csv = CsvTable::read(path);
    csv.require_columns_in({"name", "quotes", "discount", "reference", "interp", "extrapolation",
                            "calendar", "spot_lag", "payment_lag"});
    const std::filesystem::path base = std::filesystem::path(path).parent_path();
    std::vector<CurveSpec> specs;
    for (std::size_t i = 0; i < csv.size(); ++i) {
        try {
            CurveSpec s;
            s.name = csv.at(i, "name");
            s.discount = csv.get(i, "discount").value_or("");
            s.reference = csv.get(i, "reference").value_or("");
            s.calendar = defaults.calendar;
            if (const auto calendar = csv.get(i, "calendar")) {
                s.calendar = Calendar::from_name(*calendar);
            }
            s.options.method = defaults.method;
            if (const auto interp = csv.get(i, "interp")) {
                s.options.method = parse_interpolation_method(*interp);
            }
            s.options.extrapolation = defaults.extrapolation;
            if (const auto extrapolation = csv.get(i, "extrapolation")) {
                s.options.extrapolation = parse_extrapolation(*extrapolation);
            }
            QuoteConventions conventions;
            conventions.calendar = s.calendar;
            conventions.settle_days = defaults.settle_days;
            if (const auto lag = csv.get(i, "spot_lag")) {
                conventions.settle_days = parse_int(*lag, "spot_lag");
                require(conventions.settle_days >= 0, "spot_lag must be non-negative");
            }
            conventions.payment_lag = defaults.payment_lag;
            if (const auto lag = csv.get(i, "payment_lag")) {
                conventions.payment_lag = parse_int(*lag, "payment_lag");
                require(conventions.payment_lag >= 0, "payment_lag must be non-negative");
            }
            std::filesystem::path quotes(csv.at(i, "quotes"));
            if (quotes.is_relative()) {
                quotes = base / quotes;
            }
            s.quotes = load_quotes(quotes.string(), as_of, conventions);
            specs.push_back(std::move(s));
        } catch (const Error& e) {
            const std::string what = e.what();
            throw Error(what.starts_with(path) ? what : csv.where(i) + ": " + what);
        }
    }
    require(!specs.empty(), path + ": no curves found");
    return specs;
}

} // namespace fit

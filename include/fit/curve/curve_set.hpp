#pragma once

#include "fit/curve/bootstrap.hpp"
#include "fit/time/calendar.hpp"
#include "fit/time/date.hpp"

#include <optional>
#include <string>
#include <vector>

namespace fit {

/// One curve of a set, before it is built (ADR 0017).
struct CurveSpec {
    std::string name;
    std::vector<Quote> quotes;
    /// The name of the curve that discounts this one; empty, or the curve's own name, for a
    /// curve that discounts itself.
    std::string discount;
    Calendar calendar;        ///< for the instruments' schedules
    BootstrapOptions options; ///< `options.discount` is filled in when the set is built
};

/// A built curve under its name.
struct NamedCurve {
    std::string name;
    std::string discount_name; ///< the curve it is discounted on (its own name if itself)
    Calendar calendar;         ///< the calendar its instruments were scheduled on
    BootstrapResult bootstrap;
};

/// Builds every curve of `specs`, each after the curve that discounts it (ADR 0016), keeping the
/// given order where dependencies allow. Throws fit::Error for an empty set, duplicate or empty
/// names, an unknown discount curve, or curves that discount each other in a cycle. When the
/// set has more than one curve, errors from a curve's bootstrap name the curve.
std::vector<NamedCurve> build_curve_set(Date as_of, const std::vector<CurveSpec>& specs);

/// Settings a curve-set file's rows inherit unless they give their own.
struct CurveSetDefaults {
    InterpolationMethod method = InterpolationMethod::FlatForward;
    std::optional<Extrapolation> extrapolation;
    Calendar calendar;
    int settle_days = 0;
};

/// Reads a curve-set CSV with columns `name,quotes` and optional `discount`, `interp`,
/// `extrapolation`, `calendar` and `spot_lag`, and loads each row's quotes file. Relative
/// quotes paths are relative to the set file's directory. A row's `calendar` (none, weekends
/// or us) replaces the default calendar, holidays included, for that curve.
std::vector<CurveSpec> load_curve_set(const std::string& path, Date as_of,
                                      const CurveSetDefaults& defaults);

} // namespace fit

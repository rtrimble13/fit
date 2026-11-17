#pragma once

#include "args.hpp"
#include "fit/bond/analytics.hpp"
#include "fit/bond/bond.hpp"
#include "fit/curve/bootstrap.hpp"
#include "fit/curve/yield_curve.hpp"
#include "fit/io/table.hpp"
#include "fit/time/calendar.hpp"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fit::cli {

/// One `fit <group> <name>` command. A command with an empty `name` is invoked as
/// `fit <group>`.
struct Command {
    std::string group;
    std::string name;
    std::string summary;
    std::vector<OptionSpec> options;
    std::string example;
    std::function<void(const Args&, Report&)> run;
};

std::vector<Command> curve_commands();
std::vector<Command> bond_commands();
std::vector<Command> portfolio_commands();

// ---- Shared option groups and their loaders ----------------------------------------------------

/// `--curve` (or `--discount-curve` with `--projection-curve`) `--asof --interp
/// --extrapolation --calendar --holidays --spot-lag`. With `required`, a curve source and
/// `--asof` must be given.
std::vector<OptionSpec> curve_options(bool required);
/// `--maturity --coupon --frequency --daycount --issue --settle --settle-days --no-eom`.
std::vector<OptionSpec> bond_options();
/// `--price --yield --zspread`.
std::vector<OptionSpec> mark_options();
std::vector<OptionSpec> key_tenor_options();

std::vector<OptionSpec> operator+(std::vector<OptionSpec> a, const std::vector<OptionSpec>& b);

/// One bootstrapped curve of a market, under the name output refers to it by.
struct NamedCurve {
    std::string name;
    std::string discount_name; ///< the curve it is discounted on (its own name if itself)
    BootstrapResult bootstrap;
};

/// The curves a command works with (ADR 0016).
struct Market {
    Date as_of;
    Calendar calendar;
    std::vector<NamedCurve> curves; ///< in build order: each after its discount curve
    bool multi_curve = false;       ///< built from --discount-curve and --projection-curve
    CurvePtr curve;    ///< the curve `curve` commands report: the projection curve if any
    CurvePtr discount; ///< the curve that discounts it, and every bond
};

Calendar load_calendar(const Args& args);
Market load_market(const Args& args);
/// Loads the market when `--curve` was given.
std::optional<Market> load_optional_market(const Args& args);

Bond make_bond(const Args& args, const Calendar& calendar);
/// `--settle` when given, otherwise `--asof` advanced by the bond's settlement lag.
Date settlement_date(const Args& args, const Bond& bond);
/// The mark selected by `--price`, `--yield` or `--zspread`; defaults to the curve with no
/// spread. Throws `UsageError` when more than one is given.
Mark make_mark(const Args& args, bool have_curve);

struct KeyTenors {
    std::vector<std::string> labels;
    std::vector<double> years;
};
KeyTenors parse_key_tenors(const Args& args);

struct NamedShift {
    std::string label;
    ZeroShift shift;
};
/// Parses a comma-separated scenario list. Each item is a parallel shift in bp ("-50") or
/// a piecewise-linear twist written as tenor:bp pairs joined by '/' ("2Y:-25/10Y:25").
std::vector<NamedShift> parse_shifts(const std::string& text);

/// Parses an ISO date, or a tenor added to `base`.
Date parse_date_or_tenor(const std::string& text, Date base);

double percent_option(const Args& args, const std::string& name);

} // namespace fit::cli

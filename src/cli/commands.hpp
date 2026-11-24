#pragma once

#include "args.hpp"
#include "fit/bond/analytics.hpp"
#include "fit/bond/bond.hpp"
#include "fit/curve/bootstrap.hpp"
#include "fit/curve/curve_set.hpp"
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

/// A curve source (`--curve`, `--discount-curve` with `--projection-curve`, or `--curve-set`)
/// and `--asof --interp --extrapolation --calendar --holidays --spot-lag`. With `required`, a
/// curve source and `--asof` must be given; with `select`, `--curve-name` picks the curve the
/// command uses.
std::vector<OptionSpec> curve_options(bool required, bool select = true);
/// `--maturity --coupon --frequency --daycount --issue --settle --settle-days --no-eom`.
std::vector<OptionSpec> bond_options();
/// `--price --yield --zspread`.
std::vector<OptionSpec> mark_options();
std::vector<OptionSpec> key_tenor_options();

std::vector<OptionSpec> operator+(std::vector<OptionSpec> a, const std::vector<OptionSpec>& b);

/// The curves a command works with (ADRs 0016 and 0017).
struct Market {
    Date as_of;
    Calendar calendar;
    std::vector<NamedCurve> curves; ///< in build order: each after its discount curve
    bool multi_curve = false;       ///< built from the discount/projection pair or a curve set
    CurvePtr curve;    ///< the selected curve (`--curve-name`), which curve commands report
    CurvePtr discount; ///< the curve that discounts it, and every bond
};

Calendar load_calendar(const Args& args);
/// Builds every curve of the market. With `select`, also resolves `--curve-name` into
/// `curve` and `discount`; otherwise those are left empty.
Market load_market(const Args& args, bool select = true);
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

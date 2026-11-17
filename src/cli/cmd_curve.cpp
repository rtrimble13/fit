#include "commands.hpp"
#include "fit/core/error.hpp"
#include "fit/io/csv.hpp"

#include <cmath>
#include <memory>

namespace fit::cli {

namespace {

// One row per quote of `b`, led by the curve's name when the market has several curves.
void add_nodes(Table& nodes, const NamedCurve* named, const BootstrapResult& b) {
    for (std::size_t i = 0; i < b.quotes.size(); ++i) {
        const Quote& q = b.quotes[i];
        std::vector<Cell> row;
        if (named != nullptr) {
            row.emplace_back(named->name);
        }
        for (Cell cell : std::vector<Cell>{
                 q.label, to_string(q.type), q.pillar.to_string(), b.curve->times()[i],
                 q.rate * 100.0, b.curve->zero_rates()[i] * 100.0, b.curve->discount(q.pillar),
                 (b.implied[i] - q.rate) * 1e4}) {
            row.push_back(std::move(cell));
        }
        nodes.row(std::move(row));
    }
}

void node_columns(Table& nodes) {
    nodes.column("tenor").column("type").column("pillar").column("years", 4);
    nodes.column("quote_pct", 4).column("zero_cc_pct", 4).column("discount", 6);
    nodes.column("error_bp", 6);
}

// Several curves: one summary row per curve and one nodes table led by the curve's name.
void multi_curve_build(const Market& m, Report& report) {
    Table& curves = report.add(Table("curves"));
    curves.column("curve").column("discount_curve").column("interpolation");
    curves.column("extrapolation").column("nodes", 0).column("bootstrap_passes", 0);
    curves.column("max_repricing_error_bp", 8);
    for (const NamedCurve& c : m.curves) {
        const BootstrapResult& b = c.bootstrap;
        curves.row({c.name, c.discount_name, to_string(b.curve->method()),
                    to_string(b.curve->extrapolation()), static_cast<double>(b.quotes.size()),
                    static_cast<double>(b.passes), b.max_error * 1e4});
    }
    Table& summary = report.add(Table::key_value("market"));
    summary.add("as_of", m.as_of.to_string());
    summary.add("calendar", m.calendar.name());
    Table& nodes = report.add(Table("nodes"));
    nodes.column("curve");
    node_columns(nodes);
    for (const NamedCurve& c : m.curves) {
        add_nodes(nodes, &c, c.bootstrap);
    }
}

void curve_build(const Args& args, Report& report) {
    const Market m = load_market(args);
    if (m.multi_curve) {
        multi_curve_build(m, report);
        return;
    }
    const BootstrapResult& b = m.curves.front().bootstrap;

    Table& summary = report.add(Table::key_value("curve"));
    summary.add("as_of", m.as_of.to_string());
    summary.add("interpolation", to_string(b.curve->method()));
    summary.add("extrapolation", to_string(b.curve->extrapolation()));
    summary.add("calendar", m.calendar.name());
    summary.add("nodes", static_cast<double>(b.quotes.size()), 0);
    summary.add("bootstrap_passes", b.passes, 0);
    summary.add("max_repricing_error_bp", b.max_error * 1e4, 8);

    Table& nodes = report.add(Table("nodes"));
    node_columns(nodes);
    add_nodes(nodes, nullptr, b);
}

void curve_query(const Args& args, Report& report) {
    const Market m = load_market(args);
    CurvePtr curve = m.curve;
    if (const auto horizon = args.get("horizon")) {
        curve =
            std::make_shared<ForwardImpliedCurve>(curve, parse_date_or_tenor(*horizon, m.as_of));
    }
    const Date base = curve->as_of();
    const std::vector<std::string> points =
        split_list(args.has("dates") ? args.require("dates") : args.require("tenors"));
    require(!points.empty(), "no tenors or dates to query");

    Table& t = report.add(Table(args.has("horizon") ? "forward_curve" : "curve"));
    t.column("tenor").column("date").column("years", 4).column("zero_cc_pct", 4);
    t.column("zero_sa_pct", 4).column("par_sa_pct", 4).column("fwd_inst_pct", 4);
    t.column("discount", 6);
    for (const std::string& point : points) {
        const Date date = parse_date_or_tenor(point, base);
        require(date > base, "query point '" + point + "' is not after " + base.to_string());
        t.row({point, date.to_string(), curve->time(date), curve->zero_rate(date) * 100.0,
               curve->zero_rate_compounded(date, 2) * 100.0,
               par_rate(*curve, base, date, 2, DayCount::ActActICMA, m.calendar,
                        BusinessDayConvention::Following) *
                   100.0,
               curve->instantaneous_forward_t(curve->time(date)) * 100.0, curve->discount(date)});
    }
}

void curve_forwards(const Args& args, Report& report) {
    const Market m = load_market(args);
    const Period tenor = Period::parse(args.require("tenor"));
    require(tenor.length > 0, "--tenor must be positive");
    const Date end = parse_date_or_tenor(args.require("end"), m.as_of);
    const DayCount day_count = parse_day_count(args.require("daycount"));
    require(day_count != DayCount::ActActICMA, "--daycount act/act is not valid for forwards");

    Table& t = report.add(Table("forwards"));
    t.column("start").column("end").column("start_years", 4).column("fwd_simple_pct", 4);
    t.column("fwd_cc_pct", 4).column("zero_cc_pct", 4).column("discount", 6);
    // Period boundaries are computed from the as-of date so month-end clamping never drifts.
    for (int k = 0;; ++k) {
        const Date start = m.as_of.add({tenor.length * k, tenor.unit});
        const Date stop = m.as_of.add({tenor.length * (k + 1), tenor.unit});
        if (stop > end) {
            break;
        }
        t.row({start.to_string(), stop.to_string(), m.curve->time(start),
               m.curve->simple_forward_rate(start, stop, day_count) * 100.0,
               m.curve->forward_rate(start, stop) * 100.0, m.curve->zero_rate(stop) * 100.0,
               m.curve->discount(stop)});
    }
}

} // namespace

std::vector<Command> curve_commands() {
    const std::string example_curve = "--curve examples/data/usd_treasury.csv --asof 2026-11-17";
    return {
        {"curve", "build", "Bootstrap a zero curve from market quotes and show its nodes",
         curve_options(true), "fit curve build " + example_curve + " --interp monotone-cubic",
         curve_build},
        {"curve", "query",
         "Interpolate zero, par and forward rates and discount factors at tenors or dates",
         curve_options(true) +
             std::vector<OptionSpec>{
                 {"tenors", "LIST", "tenors to query", "1M,3M,6M,1Y,2Y,3Y,5Y,7Y,10Y,20Y,30Y",
                  false},
                 {"dates", "LIST", "dates to query (overrides --tenors)", "", false},
                 {"horizon", "DATE|TENOR",
                  "query the forward curve implied for this future date instead", "", false}},
         "fit curve query " + example_curve + " --tenors 2Y,5Y,10Y --horizon 1Y", curve_query},
        {"curve", "forwards", "Tabulate a strip of consecutive forward rates",
         curve_options(true) +
             std::vector<OptionSpec>{
                 {"tenor", "TENOR", "length of each forward period", "3M", false},
                 {"end", "DATE|TENOR", "last period end", "5Y", false},
                 {"daycount", "DC", "day count of the simple forward rate", "act/360", false}},
         "fit curve forwards " + example_curve + " --tenor 6M --end 3Y", curve_forwards},
    };
}

} // namespace fit::cli

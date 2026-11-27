#include "commands.hpp"
#include "fit/core/error.hpp"
#include "fit/io/csv.hpp"

#include <cmath>
#include <memory>

namespace fit::cli {

namespace {

// One row per quote of `c`, led by the curve's name when the market has several curves. For a
// fitted curve the years, zero rate and discount factor are the curve's at the pillar, and the
// error is the fit's residual.
void add_nodes(Table& nodes, const NamedCurve& c, bool named) {
    const std::vector<Quote>& quotes = c.fit ? c.fit->quotes : c.bootstrap.quotes;
    const std::vector<double>& implied = c.fit ? c.fit->implied : c.bootstrap.implied;
    for (std::size_t i = 0; i < quotes.size(); ++i) {
        const Quote& q = quotes[i];
        std::vector<Cell> row;
        if (named) {
            row.emplace_back(c.name);
        }
        const double years = c.fit ? c.curve->time(q.pillar) : c.bootstrap.curve->times()[i];
        const double zero =
            c.fit ? c.curve->zero_rate(q.pillar) : c.bootstrap.curve->zero_rates()[i];
        for (Cell cell : std::vector<Cell>{
                 q.label, to_string(q.type), q.pillar.to_string(), years, q.rate * 100.0,
                 zero * 100.0, c.curve->discount(q.pillar), (implied[i] - q.rate) * 1e4}) {
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

constexpr int kParameterDecimals = 6;

// A fitted curve's parameters, residual statistics and iterations as key-value rows.
void add_fit_summary(Table& summary, const FitResult& fit) {
    const ParametricParameters& p = fit.curve->parameters();
    const bool svensson = fit.curve->model() == ParametricModel::Svensson;
    summary.add("beta0_pct", p.beta0 * 100.0, kParameterDecimals);
    summary.add("beta1_pct", p.beta1 * 100.0, kParameterDecimals);
    summary.add("beta2_pct", p.beta2 * 100.0, kParameterDecimals);
    if (svensson) {
        summary.add("beta3_pct", p.beta3 * 100.0, kParameterDecimals);
    }
    summary.add("tau1_years", p.tau1, kParameterDecimals);
    if (svensson) {
        summary.add("tau2_years", p.tau2, kParameterDecimals);
    }
    summary.add("rmse_bp", fit.rmse * 1e4, 4);
    summary.add("max_abs_error_bp", fit.max_error * 1e4, 4);
    summary.add("fit_iterations", fit.iterations, 0);
}

// Several curves: one summary row per curve and one nodes table led by the curve's name. Fitted
// curves report their model as their interpolation, no extrapolation and no bootstrap passes,
// and their parameters in a `fits` table.
void multi_curve_build(const Market& m, Report& report) {
    Table& curves = report.add(Table("curves"));
    curves.column("curve").column("discount_curve").column("interpolation");
    curves.column("extrapolation").column("calendar").column("nodes", 0);
    curves.column("bootstrap_passes", 0).column("max_repricing_error_bp", 8);
    bool fitted = false;
    for (const NamedCurve& c : m.curves) {
        if (c.fit) {
            fitted = true;
            curves.row({c.name, c.discount_name, to_string(c.fit->curve->model()), std::string(),
                        c.calendar.name(), static_cast<double>(c.fit->quotes.size()), 0.0,
                        c.fit->max_error * 1e4});
            continue;
        }
        const BootstrapResult& b = c.bootstrap;
        curves.row({c.name, c.discount_name, to_string(b.curve->method()),
                    to_string(b.curve->extrapolation()), c.calendar.name(),
                    static_cast<double>(b.quotes.size()), static_cast<double>(b.passes),
                    b.max_error * 1e4});
    }
    if (fitted) {
        Table& fits = report.add(Table("fits"));
        fits.column("curve").column("model");
        for (const char* beta : {"beta0_pct", "beta1_pct", "beta2_pct", "beta3_pct"}) {
            fits.column(beta, kParameterDecimals);
        }
        fits.column("tau1_years", kParameterDecimals).column("tau2_years", kParameterDecimals);
        fits.column("rmse_bp", 4).column("max_abs_error_bp", 4).column("fit_iterations", 0);
        for (const NamedCurve& c : m.curves) {
            if (!c.fit) {
                continue;
            }
            const ParametricParameters& p = c.fit->curve->parameters();
            const bool svensson = c.fit->curve->model() == ParametricModel::Svensson;
            fits.row({c.name, to_string(c.fit->curve->model()), p.beta0 * 100.0, p.beta1 * 100.0,
                      p.beta2 * 100.0, svensson ? Cell(p.beta3 * 100.0) : Cell(std::string()),
                      p.tau1, svensson ? Cell(p.tau2) : Cell(std::string()), c.fit->rmse * 1e4,
                      c.fit->max_error * 1e4, static_cast<double>(c.fit->iterations)});
        }
    }
    Table& summary = report.add(Table::key_value("market"));
    summary.add("as_of", m.as_of.to_string());
    summary.add("calendar", m.calendar.name());
    Table& nodes = report.add(Table("nodes"));
    nodes.column("curve");
    node_columns(nodes);
    for (const NamedCurve& c : m.curves) {
        add_nodes(nodes, c, true);
    }
}

void curve_build(const Args& args, Report& report) {
    const Market m = load_market(args, false);
    if (m.multi_curve) {
        multi_curve_build(m, report);
        return;
    }
    const NamedCurve& c = m.curves.front();
    Table& summary = report.add(Table::key_value("curve"));
    summary.add("as_of", m.as_of.to_string());
    if (c.fit) {
        summary.add("fit", to_string(c.fit->curve->model()));
        summary.add("calendar", m.calendar.name());
        summary.add("nodes", static_cast<double>(c.fit->quotes.size()), 0);
        add_fit_summary(summary, *c.fit);
    } else {
        const BootstrapResult& b = c.bootstrap;
        summary.add("interpolation", to_string(b.curve->method()));
        summary.add("extrapolation", to_string(b.curve->extrapolation()));
        summary.add("calendar", m.calendar.name());
        summary.add("nodes", static_cast<double>(b.quotes.size()), 0);
        summary.add("bootstrap_passes", b.passes, 0);
        summary.add("max_repricing_error_bp", b.max_error * 1e4, 8);
    }

    Table& nodes = report.add(Table("nodes"));
    node_columns(nodes);
    add_nodes(nodes, c, false);
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
        {"curve", "build",
         "Build a zero curve from market quotes, by bootstrap or by fit, and show its nodes",
         curve_options(true, false),
         "fit curve build " + example_curve + " --interp monotone-cubic", curve_build},
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

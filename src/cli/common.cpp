#include "commands.hpp"
#include "fit/core/error.hpp"
#include "fit/io/csv.hpp"

namespace fit::cli {

std::vector<OptionSpec> operator+(std::vector<OptionSpec> a, const std::vector<OptionSpec>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

std::vector<OptionSpec> curve_options(bool required) {
    return {
        {"curve", "FILE", "market quotes CSV (columns: type,tenor,rate)", "", required},
        {"asof", "DATE", "curve / valuation date (YYYY-MM-DD)", "", required},
        {"interp", "METHOD",
         "linear-zero | flat-forward | cubic-zero | monotone-cubic | monotone-convex",
         "flat-forward", false},
        {"extrapolation", "METHOD",
         "beyond the last pillar: flat-forward | flat-zero (default: flat-zero for linear-zero "
         "and cubic-zero, flat-forward otherwise)",
         "", false},
        {"calendar", "NAME", "business-day calendar: none | weekends | us", "weekends", false},
        {"holidays", "FILE", "extra holidays, one ISO date per line", "", false},
        {"spot-lag", "DAYS", "business days from as-of to quote start dates", "0", false},
    };
}

std::vector<OptionSpec> bond_options() {
    return {
        {"maturity", "DATE", "maturity date", "", true},
        {"coupon", "PCT", "annual coupon rate in percent (0 for a zero-coupon bond)", "", true},
        {"frequency", "N", "coupons per year: 1, 2, 4 or 12", "2", false},
        {"daycount", "DC", "act/act | act/act-isda | 30/360 | 30e/360 | act/360 | act/365",
         "act/act", false},
        {"issue", "DATE", "dated date (default: regular schedule back from maturity)", "", false},
        {"settle", "DATE", "settlement date (default: as-of plus --settle-days)", "", false},
        {"settle-days", "N", "settlement lag in business days", "1", false},
        {"no-eom", "", "do not roll on month ends when maturity is a month end", "", false},
    };
}

std::vector<OptionSpec> mark_options() {
    return {
        {"price", "PX", "clean price per 100 face", "", false},
        {"yield", "PCT", "yield to maturity in percent (street convention)", "", false},
        {"zspread", "BP", "z-spread over the curve in basis points", "", false},
    };
}

std::vector<OptionSpec> key_tenor_options() {
    return {{"key-tenors", "LIST", "key-rate tenors", "3M,6M,1Y,2Y,3Y,5Y,7Y,10Y,20Y,30Y", false}};
}

Calendar load_calendar(const Args& args) {
    Calendar calendar = Calendar::from_name(args.require("calendar"));
    if (const auto file = args.get("holidays")) {
        calendar.load_holidays(*file);
    }
    return calendar;
}

Market load_market(const Args& args) {
    Market m;
    m.as_of = Date::parse(args.require("asof"));
    m.calendar = load_calendar(args);
    QuoteConventions conventions;
    conventions.calendar = m.calendar;
    conventions.settle_days = parse_int(args.require("spot-lag"), "--spot-lag");
    require(conventions.settle_days >= 0, "--spot-lag must be non-negative");
    BootstrapOptions options;
    options.method = parse_interpolation_method(args.require("interp"));
    if (const auto extrapolation = args.get("extrapolation")) {
        options.extrapolation = parse_extrapolation(*extrapolation);
    }
    m.bootstrap = bootstrap(m.as_of, load_quotes(args.require("curve"), m.as_of, conventions),
                            m.calendar, options);
    m.curve = m.bootstrap.curve;
    return m;
}

std::optional<Market> load_optional_market(const Args& args) {
    if (!args.has("curve")) {
        return std::nullopt;
    }
    return load_market(args);
}

double percent_option(const Args& args, const std::string& name) {
    return parse_double(args.require(name), "--" + name) / 100.0;
}

Bond make_bond(const Args& args, const Calendar& calendar) {
    BondSpec spec;
    spec.maturity = Date::parse(args.require("maturity"));
    spec.coupon = percent_option(args, "coupon");
    spec.frequency = parse_int(args.require("frequency"), "--frequency");
    spec.day_count = parse_day_count(args.require("daycount"));
    spec.calendar = calendar;
    spec.end_of_month = !args.has("no-eom");
    spec.settle_days = parse_int(args.require("settle-days"), "--settle-days");
    if (const auto issue = args.get("issue")) {
        spec.issue = Date::parse(*issue);
    }
    return Bond(spec);
}

Date settlement_date(const Args& args, const Bond& bond) {
    if (const auto settle = args.get("settle")) {
        return Date::parse(*settle);
    }
    if (const auto as_of = args.get("asof")) {
        return bond.settlement_date(Date::parse(*as_of));
    }
    throw UsageError("give --settle, or --asof to derive the settlement date");
}

Mark make_mark(const Args& args, bool have_curve) {
    const int given = int{args.has("price")} + int{args.has("yield")} + int{args.has("zspread")};
    if (given > 1) {
        throw UsageError("give at most one of --price, --yield and --zspread");
    }
    if (args.has("price")) {
        return Mark::clean_price(parse_double(args.require("price"), "--price"));
    }
    if (args.has("yield")) {
        return Mark::yield(percent_option(args, "yield"));
    }
    if (!have_curve) {
        throw UsageError(args.has("zspread") ? "--zspread requires --curve"
                                             : "give --price or --yield, or --curve to price "
                                               "off the curve");
    }
    return Mark::curve(
        args.has("zspread") ? parse_double(args.require("zspread"), "--zspread") / 1e4 : 0.0);
}

KeyTenors parse_key_tenors(const Args& args) {
    KeyTenors out;
    for (const std::string& item : split_list(args.require("key-tenors"))) {
        const Period p = Period::parse(item);
        require(p.length > 0, "key tenor '" + item + "' must be positive");
        out.labels.push_back(p.to_string());
        out.years.push_back(p.approx_years());
    }
    require(!out.years.empty(), "--key-tenors is empty");
    for (std::size_t i = 1; i < out.years.size(); ++i) {
        require(out.years[i] > out.years[i - 1], "--key-tenors must be strictly increasing");
    }
    return out;
}

std::vector<NamedShift> parse_shifts(const std::string& text) {
    std::vector<NamedShift> out;
    for (const std::string& item : split_list(text)) {
        if (item.find(':') == std::string::npos) {
            const double bp = parse_double(item, "shift");
            out.push_back({format_number(bp, 0) + "bp", ZeroShift::parallel(bp / 1e4)});
            continue;
        }
        std::vector<double> times;
        std::vector<double> spreads;
        for (const std::string& node : split_list(item, '/')) {
            const std::size_t colon = node.find(':');
            require(colon != std::string::npos,
                    "invalid shift '" + item + "' (expected tenor:bp pairs joined by '/')");
            times.push_back(Period::parse(trim(node.substr(0, colon))).approx_years());
            spreads.push_back(parse_double(node.substr(colon + 1), "shift") / 1e4);
        }
        out.push_back({item, ZeroShift::piecewise(std::move(times), std::move(spreads))});
    }
    require(!out.empty(), "no shifts given");
    return out;
}

Date parse_date_or_tenor(const std::string& text, Date base) {
    return Date::looks_like(text) ? Date::parse(text) : base.add(Period::parse(text));
}

} // namespace fit::cli

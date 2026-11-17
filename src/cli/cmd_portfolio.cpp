#include "commands.hpp"
#include "fit/core/error.hpp"
#include "fit/io/csv.hpp"
#include "fit/portfolio/portfolio.hpp"

#include <cmath>
#include <map>

namespace fit::cli {

namespace {

const std::vector<OptionSpec> kPortfolio = {
    {"portfolio", "FILE", "positions CSV (columns: id,face,maturity,coupon,...)", "", true},
    {"trades", "FILE", "proposed trades CSV to add to the portfolio (what-if)", "", false},
    {"settle", "DATE", "valuation settlement date (default: as-of date)", "", false},
};

/// The positions in `--portfolio`, followed by any proposed trades in `--trades`.
std::vector<Position> load_book(const Args& args, const Calendar& calendar) {
    std::vector<Position> book = load_positions(args.require("portfolio"), calendar);
    if (const auto file = args.get("trades")) {
        for (Position& trade : load_positions(*file, calendar)) {
            book.push_back(std::move(trade));
        }
    }
    return book;
}

Date valuation_date(const Args& args, const Market& market) {
    const auto settle = args.get("settle");
    return settle ? Date::parse(*settle) : market.as_of;
}

void add_totals(Table& t, const PortfolioValuation& v) {
    t.add("settle", v.settle.to_string());
    t.add("positions", static_cast<double>(v.positions.size()), 0);
    t.add("face", v.face, 2, true);
    t.add("market_value", v.market_value, 2, true);
    t.add("accrued", v.accrued_value, 2, true);
    t.add("dv01", v.dv01, 2, true);
    t.add("duration", v.duration, 4);
    t.add("convexity", v.convexity, 4);
    t.add("yield_pct", v.yield * 100.0, 4);
}

void add_positions(Table& t, const PortfolioValuation& v) {
    t.column("id").column("face", 0, true).column("clean_price", 4).column("yield_pct", 4);
    t.column("zspread_bp", 2).column("market_value", 2, true).column("accrued", 2, true);
    t.column("duration", 3).column("dv01", 2, true);
    for (const PositionValuation& p : v.positions) {
        t.row({p.id, p.face, p.bond.clean, p.bond.yield * 100.0, *p.bond.z_spread * 1e4,
               p.market_value, p.accrued_value, p.effective_duration, p.dv01});
    }
}

void portfolio_value(const Args& args, Report& report) {
    const Market market = load_market(args);
    const auto positions = load_book(args, market.calendar);
    const PortfolioValuation v =
        value_portfolio(positions, valuation_date(args, market), market.curve);
    add_positions(report.add(Table("positions")), v);
    add_totals(report.add(Table::key_value("totals")), v);
}

void portfolio_risk(const Args& args, Report& report) {
    const Market market = load_market(args);
    const auto positions = load_book(args, market.calendar);
    const KeyTenors keys = parse_key_tenors(args);
    const PortfolioValuation v =
        value_portfolio(positions, valuation_date(args, market), market.curve, keys.years);

    Table& t = report.add(Table("key_rate_dv01"));
    t.column("id");
    for (const std::string& label : keys.labels) {
        t.column(label, 0, true);
    }
    t.column("total", 0, true);
    for (const PositionValuation& p : v.positions) {
        std::vector<Cell> row{p.id};
        for (const double k : p.key_dv01) {
            row.emplace_back(k);
        }
        row.emplace_back(p.dv01);
        t.row(std::move(row));
    }
    std::vector<Cell> total{std::string("TOTAL")};
    for (const double k : v.key_dv01) {
        total.emplace_back(k);
    }
    total.emplace_back(v.dv01);
    t.row(std::move(total));
    add_totals(report.add(Table::key_value("totals")), v);
}

void portfolio_scenario(const Args& args, Report& report) {
    const Market market = load_market(args);
    const auto positions = load_book(args, market.calendar);
    const PortfolioValuation v =
        value_portfolio(positions, valuation_date(args, market), market.curve);

    Table& t = report.add(Table("scenarios"));
    t.column("scenario").column("market_value", 2, true).column("pnl", 2, true);
    t.column("pnl_pct", 3);
    t.row({std::string("base"), v.market_value, 0.0, 0.0});
    for (const NamedShift& s : parse_shifts(args.require("shifts"))) {
        const double mv = shocked_market_value(positions, v, market.curve, s.shift);
        t.row({s.label, mv, mv - v.market_value,
               100.0 * (mv - v.market_value) / std::fabs(v.market_value)});
    }
}

void portfolio_cashflows_cmd(const Args& args, Report& report) {
    const Market market = load_market(args);
    const auto positions = load_book(args, market.calendar);
    const Date settle = valuation_date(args, market);
    const std::string bucket = to_lower(args.require("bucket"));
    require(bucket == "none" || bucket == "month" || bucket == "year",
            "unknown --bucket '" + bucket + "' (expected none, month or year)");

    struct Sum {
        double coupon = 0.0;
        double principal = 0.0;
        double pv = 0.0;
    };
    std::map<std::string, Sum> buckets;
    for (const DatedFlow& f : portfolio_cashflows(positions, settle)) {
        const std::string date = f.payment.to_string();
        const std::string key = bucket == "none" ? date : date.substr(0, bucket == "month" ? 7 : 4);
        Sum& s = buckets[key];
        s.coupon += f.coupon;
        s.principal += f.principal;
        s.pv += (f.coupon + f.principal) * market.curve->discount(f.payment) /
                market.curve->discount(settle);
    }
    Table& t = report.add(Table("cashflows"));
    t.column(bucket == "none" ? "payment" : bucket).column("coupon", 2, true);
    t.column("principal", 2, true).column("total", 2, true).column("curve_pv", 2, true);
    Sum total;
    for (const auto& [key, s] : buckets) {
        t.row({key, s.coupon, s.principal, s.coupon + s.principal, s.pv});
        total.coupon += s.coupon;
        total.principal += s.principal;
        total.pv += s.pv;
    }
    t.row({std::string("TOTAL"), total.coupon, total.principal, total.coupon + total.principal,
           total.pv});
}

void pretrade(const Args& args, Report& report) {
    const Market market = load_market(args);
    const Date settle = valuation_date(args, market);
    const KeyTenors keys = parse_key_tenors(args);
    const auto trades = load_positions(args.require("trades"), market.calendar);
    require(!trades.empty(), "no trades found in '" + args.require("trades") + "'");
    std::vector<Position> before_positions;
    if (const auto file = args.get("portfolio")) {
        before_positions = load_positions(*file, market.calendar);
    }
    std::vector<Position> after_positions = before_positions;
    after_positions.insert(after_positions.end(), trades.begin(), trades.end());

    const PortfolioValuation before =
        value_portfolio(before_positions, settle, market.curve, keys.years);
    const PortfolioValuation trade = value_portfolio(trades, settle, market.curve, keys.years);
    const PortfolioValuation after =
        value_portfolio(after_positions, settle, market.curve, keys.years);

    Table& tickets = report.add(Table("trades"));
    tickets.column("id").column("side").column("face", 0, true).column("clean_price", 4);
    tickets.column("accrued", 4).column("yield_pct", 4).column("zspread_bp", 2);
    tickets.column("settlement_amount", 2, true).column("duration", 3).column("dv01", 2, true);
    for (const PositionValuation& p : trade.positions) {
        tickets.row({p.id, std::string(p.face >= 0.0 ? "BUY" : "SELL"), std::fabs(p.face),
                     p.bond.clean, p.bond.accrued, p.bond.yield * 100.0, *p.bond.z_spread * 1e4,
                     p.market_value, p.effective_duration, p.dv01});
    }

    Table& impact = report.add(Table("impact"));
    impact.column("metric").column("before", 2, true).column("trade", 2, true);
    impact.column("after", 2, true).column("change", 2, true);
    const auto line = [&](const std::string& metric, double b, double t, double a) {
        impact.row({metric, b, t, a, a - b});
    };
    line("market_value", before.market_value, trade.market_value, after.market_value);
    line("dv01", before.dv01, trade.dv01, after.dv01);
    for (std::size_t k = 0; k < keys.labels.size(); ++k) {
        line("kr_dv01_" + keys.labels[k], before.key_dv01[k], trade.key_dv01[k], after.key_dv01[k]);
    }

    // Ratios are not additive, so there is no meaningful "trade" column for them.
    Table& ratios = report.add(Table("ratios"));
    ratios.column("metric").column("before", 4).column("after", 4).column("change", 4);
    ratios.row({std::string("duration"), before.duration, after.duration,
                after.duration - before.duration});
    ratios.row({std::string("convexity"), before.convexity, after.convexity,
                after.convexity - before.convexity});
    ratios.row({std::string("yield_pct"), before.yield * 100.0, after.yield * 100.0,
                (after.yield - before.yield) * 100.0});

    if (args.has("dv01-limit")) {
        const double limit = parse_double(args.require("dv01-limit"), "--dv01-limit");
        require(limit > 0.0, "--dv01-limit must be positive");
        const double used = std::fabs(after.dv01);
        Table& limits = report.add(Table::key_value("limits"));
        limits.add("dv01_limit", limit, 2, true);
        limits.add("dv01_before", std::fabs(before.dv01), 2, true);
        limits.add("dv01_after", used, 2, true);
        limits.add("headroom", limit - used, 2, true);
        limits.add("utilisation_pct", 100.0 * used / limit, 1);
        limits.add("status", used <= limit ? "OK" : "BREACH");
        if (used > limit) {
            report.warn("DV01 limit breached: " + format_number(used, 2, true) + " > " +
                        format_number(limit, 2, true));
        }
    }
}

} // namespace

std::vector<Command> portfolio_commands() {
    const std::string inputs = "--portfolio examples/data/portfolio.csv "
                               "--curve examples/data/usd_treasury.csv --asof 2026-11-17";
    const std::vector<OptionSpec> base = kPortfolio + curve_options(true);
    return {
        {"portfolio", "value", "Mark every position and aggregate value, yield and duration", base,
         "fit portfolio value " + inputs, portfolio_value},
        {"portfolio", "risk", "Key-rate DV01 ladder by position and in total",
         base + key_tenor_options(), "fit portfolio risk " + inputs + " --key-tenors 2Y,5Y,10Y,30Y",
         portfolio_risk},
        {"portfolio", "scenario", "Instantaneous P&L under parallel shifts and curve twists",
         base + std::vector<OptionSpec>{{"shifts", "LIST",
                                         "zero-rate shocks in bp: parallel (-50) or twists "
                                         "(2Y:-25/10Y:25)",
                                         "-100,-50,-25,25,50,100", false}},
         "fit portfolio scenario " + inputs + " --shifts -50,50,2Y:-25/10Y:25", portfolio_scenario},
        {"portfolio", "cashflows", "Projected cashflow ladder, optionally bucketed",
         base + std::vector<OptionSpec>{{"bucket", "NAME", "none | month | year", "year", false}},
         "fit portfolio cashflows " + inputs + " --bucket year", portfolio_cashflows_cmd},
        {"pretrade", "",
         "What-if analysis of proposed trades: settlement amounts and before/after risk",
         std::vector<OptionSpec>{
             {"trades", "FILE", "proposed trades CSV (positions schema; negative face sells)", "",
              true},
             {"portfolio", "FILE", "existing positions CSV (omit to analyse trades alone)", "",
              false},
             {"settle", "DATE", "valuation settlement date (default: as-of date)", "", false},
             {"dv01-limit", "AMOUNT", "flag when |portfolio DV01| after trading exceeds this", "",
              false}} +
             curve_options(true) + key_tenor_options(),
         "fit pretrade --trades examples/data/trades.csv --portfolio examples/data/portfolio.csv "
         "--curve examples/data/usd_treasury.csv --asof 2026-11-17 --dv01-limit 60000",
         pretrade},
    };
}

} // namespace fit::cli

#include "commands.hpp"
#include "fit/bond/horizon.hpp"
#include "fit/core/error.hpp"
#include "fit/io/csv.hpp"

#include <cmath>

namespace fit::cli {

namespace {

const std::vector<OptionSpec> kFace = {
    {"face", "AMOUNT", "face amount, to report the settlement amount", "", false}};

void add_valuation(Table& t, const BondValuation& v) {
    t.add("settle", v.settle.to_string());
    t.add("clean_price", v.clean, 6);
    t.add("accrued", v.accrued, 6);
    t.add("dirty_price", v.dirty, 6);
    t.add("yield_pct", v.yield * 100.0, 5);
    if (v.z_spread) {
        t.add("zspread_bp", *v.z_spread * 1e4, 3);
    }
    t.add("macaulay_duration", v.yield_risk.macaulay_duration, 4);
    t.add("modified_duration", v.yield_risk.modified_duration, 4);
    t.add("convexity", v.yield_risk.convexity, 4);
    t.add("dv01", v.yield_risk.dv01, 6);
}

void bond_price(const Args& args, Report& report) {
    const std::optional<Market> market = load_optional_market(args);
    const Bond bond = make_bond(args, market ? market->calendar : load_calendar(args));
    const Date settle = settlement_date(args, bond);
    const Mark mark = make_mark(args, market.has_value());
    const BondValuation v =
        value_bond(bond, settle, mark, market ? market->discount.get() : nullptr);

    Table& t = report.add(Table::key_value("bond"));
    add_valuation(t, v);
    if (args.has("face")) {
        const double face = parse_double(args.require("face"), "--face");
        t.add("face", face, 2, true);
        t.add("settlement_amount", face * v.dirty / 100.0, 2, true);
        t.add("position_dv01", face * v.yield_risk.dv01 / 100.0, 2, true);
    }
}

void bond_yield(const Args& args, Report& report) {
    if (!args.has("price")) {
        throw UsageError("missing required option '--price'");
    }
    bond_price(args, report);
}

void bond_cashflows(const Args& args, Report& report) {
    const std::optional<Market> market = load_optional_market(args);
    const Bond bond = make_bond(args, market ? market->calendar : load_calendar(args));
    const Date settle = settlement_date(args, bond);

    Table& t = report.add(Table("cashflows"));
    t.column("payment").column("accrual_start").column("accrual_end");
    t.column("coupon", 6).column("principal", 4).column("amount", 6);
    std::optional<BondValuation> v;
    if (market) {
        v = value_bond(bond, settle, make_mark(args, true), market->discount.get());
        t.column("discount", 6).column("pv", 6);
    }
    double total = 0.0;
    double total_pv = 0.0;
    const auto& flows = bond.cashflows();
    for (std::size_t i = bond.first_cashflow_after(settle); i < flows.size(); ++i) {
        const Cashflow& cf = flows[i];
        std::vector<Cell> row{cf.payment.to_string(),
                              cf.accrual_start.to_string(),
                              cf.accrual_end.to_string(),
                              cf.coupon,
                              cf.principal,
                              cf.amount()};
        if (market) {
            const YieldCurve& curve = *market->discount;
            const double df =
                curve.discount(cf.payment) / curve.discount(settle) *
                std::exp(-*v->z_spread * (curve.time(cf.payment) - curve.time(settle)));
            row.emplace_back(df);
            row.emplace_back(df * cf.amount());
            total_pv += df * cf.amount();
        }
        total += cf.amount();
        t.row(std::move(row));
    }
    Table& summary = report.add(Table::key_value("summary"));
    summary.add("settle", settle.to_string());
    summary.add("accrued", bond.accrued(settle), 6);
    summary.add("total_cashflow", total, 6);
    if (market) {
        summary.add("dirty_price", total_pv, 6);
        summary.add("zspread_bp", *v->z_spread * 1e4, 3);
    }
}

void bond_risk(const Args& args, Report& report) {
    const Market market = load_market(args);
    const Bond bond = make_bond(args, market.calendar);
    const Date settle = settlement_date(args, bond);
    const BondValuation v = value_bond(bond, settle, make_mark(args, true), market.discount.get());
    const KeyTenors keys = parse_key_tenors(args);
    const double bump = parse_double(args.require("bump-bp"), "--bump-bp") / 1e4;
    const CurveRisk risk = curve_risk(bond, settle, market.discount, *v.z_spread, keys.years, bump);

    Table& t = report.add(Table::key_value("risk"));
    t.add("settle", settle.to_string());
    t.add("clean_price", v.clean, 6);
    t.add("dirty_price", v.dirty, 6);
    t.add("yield_pct", v.yield * 100.0, 5);
    t.add("zspread_bp", *v.z_spread * 1e4, 3);
    t.add("modified_duration", v.yield_risk.modified_duration, 4);
    t.add("effective_duration", risk.effective_duration, 4);
    t.add("effective_convexity", risk.effective_convexity, 4);
    t.add("yield_dv01", v.yield_risk.dv01, 6);
    t.add("curve_dv01", risk.dv01, 6);

    Table& ladder = report.add(Table("key_rates"));
    ladder.column("tenor").column("key_dv01", 6).column("pct_of_total", 2);
    double sum = 0.0;
    for (std::size_t k = 0; k < keys.labels.size(); ++k) {
        sum += risk.key_dv01[k];
        ladder.row({keys.labels[k], risk.key_dv01[k], 100.0 * risk.key_dv01[k] / risk.dv01});
    }
    ladder.row({"total", sum, 100.0 * sum / risk.dv01});
}

void bond_forecast(const Args& args, Report& report) {
    const Market market = load_market(args);
    const Bond bond = make_bond(args, market.calendar);
    const Date settle = settlement_date(args, bond);
    const Mark mark = make_mark(args, true);

    HorizonSpec spec;
    spec.horizon = parse_date_or_tenor(args.require("horizon"), settle);
    spec.base = parse_horizon_base(args.require("scenario"));
    if (args.has("reinvest")) {
        spec.reinvest_rate = percent_option(args, "reinvest");
    }

    Table& start = report.add(Table::key_value("start"));
    Table& scenarios = report.add(Table("scenarios"));
    scenarios.column("shift").column("clean_end", 4).column("yield_end_pct", 4);
    scenarios.column("coupon_income", 4).column("roll", 4).column("curve_shift", 4);
    scenarios.column("reinvestment", 4).column("total_pnl", 4).column("return_pct", 3);
    scenarios.column("annualized_pct", 3);

    bool first = true;
    for (const NamedShift& s : parse_shifts(args.require("shifts"))) {
        spec.shift = s.shift;
        const HorizonResult r = horizon_analysis(bond, settle, mark, market.discount, spec);
        if (first) {
            first = false;
            start.add("settle", r.settle.to_string());
            start.add("horizon", r.horizon.to_string());
            start.add("horizon_days", r.horizon - r.settle, 0);
            start.add("scenario", to_string(spec.base));
            start.add("clean_price", r.clean_start, 6);
            start.add("accrued", r.accrued_start, 6);
            start.add("dirty_price", r.dirty_start, 6);
            start.add("yield_pct", r.yield_start * 100.0, 5);
            start.add("zspread_bp", r.z_spread * 1e4, 3);
            start.add("coupons_to_horizon", r.coupons, 6);
            start.add("accrued_at_horizon", r.accrued_end, 6);
        }
        scenarios.row({s.label, r.clean_end, r.yield_end * 100.0, r.coupon_income, r.roll,
                       r.curve_shift, r.reinvestment, r.total_pnl, r.total_return * 100.0,
                       r.annualized_return * 100.0});
    }
}

} // namespace

std::vector<Command> bond_commands() {
    const std::string bond = "--maturity 2036-11-15 --coupon 4.25";
    const std::string curve = "--curve examples/data/usd_treasury.csv --asof 2026-11-17";
    const std::vector<OptionSpec> pricing = bond_options() + mark_options();
    return {
        {"bond", "price",
         "Price a bond from a yield, a clean price or a curve; report yield, accrued and risk",
         pricing + curve_options(false) + kFace,
         "fit bond price " + bond + " --settle 2026-11-18 --yield 4.40", bond_price},
        {"bond", "yield", "Yield and analytics from a clean price (same output as 'bond price')",
         pricing + curve_options(false) + kFace,
         "fit bond yield " + bond + " --settle 2026-11-18 --price 98.50", bond_yield},
        {"bond", "cashflows", "List remaining cashflows; with a curve, discount each one",
         pricing + curve_options(false), "fit bond cashflows " + bond + " " + curve,
         bond_cashflows},
        {"bond", "risk", "Curve risk: effective duration/convexity and key-rate DV01s",
         pricing + curve_options(true) + key_tenor_options() +
             std::vector<OptionSpec>{
                 {"bump-bp", "BP", "bump size for finite differences", "1", false}},
         "fit bond risk " + bond + " " + curve + " --price 98.50", bond_risk},
        {"bond", "forecast",
         "Horizon analysis: projected price, P&L attribution and total return by scenario",
         pricing + curve_options(true) +
             std::vector<OptionSpec>{
                 {"horizon", "DATE|TENOR", "horizon settlement date, or tenor from settlement", "",
                  true},
                 {"scenario", "NAME",
                  "horizon curve before shifts: unchanged (roll-down) | forward", "unchanged",
                  false},
                 {"shifts", "LIST",
                  "zero-rate shocks in bp: parallel (-50) or twists (2Y:-25/10Y:25)", "0", false},
                 {"reinvest", "PCT", "coupon reinvestment rate (default: curve forwards)", "",
                  false}},
         "fit bond forecast " + bond + " " + curve + " --horizon 1Y --shifts -50,0,50",
         bond_forecast},
    };
}

} // namespace fit::cli

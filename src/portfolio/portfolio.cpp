#include "fit/portfolio/portfolio.hpp"

#include "fit/core/error.hpp"
#include "fit/io/csv.hpp"

#include <limits>
#include <map>

namespace fit {

std::vector<Position> load_positions(const std::string& path, const Calendar& calendar) {
    const CsvTable csv = CsvTable::read(path);
    csv.require_columns_in(
        {"id", "face", "maturity", "coupon", "frequency", "daycount", "issue", "price", "zspread"});
    std::vector<Position> positions;
    for (std::size_t i = 0; i < csv.size(); ++i) {
        try {
            BondSpec spec;
            spec.id = csv.at(i, "id");
            spec.maturity = Date::parse(csv.at(i, "maturity"));
            spec.coupon = parse_double(csv.at(i, "coupon"), "coupon") / 100.0;
            spec.calendar = calendar;
            if (const auto v = csv.get(i, "frequency")) {
                spec.frequency = parse_int(*v, "frequency");
            }
            if (const auto v = csv.get(i, "daycount")) {
                spec.day_count = parse_day_count(*v);
            }
            if (const auto v = csv.get(i, "issue")) {
                spec.issue = Date::parse(*v);
            }
            const auto price = csv.get(i, "price");
            const auto spread = csv.get(i, "zspread");
            require(!(price && spread), "give either 'price' or 'zspread', not both");

            Position p{spec.id, parse_double(csv.at(i, "face"), "face"), Bond(spec), {}, 0.0};
            if (price) {
                p.clean_price = parse_double(*price, "price");
                require(*p.clean_price > 0.0, "price must be positive");
            }
            if (spread) {
                p.z_spread = parse_double(*spread, "zspread") / 1e4;
            }
            positions.push_back(std::move(p));
        } catch (const Error& e) {
            throw Error(csv.where(i) + ": " + e.what());
        }
    }
    return positions;
}

PortfolioValuation value_portfolio(const std::vector<Position>& positions, Date settle,
                                   const CurvePtr& curve, const std::vector<double>& key_tenors) {
    require(curve != nullptr, "portfolio valuation requires a curve");
    PortfolioValuation out;
    out.settle = settle;
    out.key_tenors = key_tenors;
    out.key_dv01.assign(key_tenors.size(), 0.0);

    double weighted_convexity = 0.0;
    double weighted_yield = 0.0;
    for (const Position& p : positions) {
        try {
            const Mark mark =
                p.clean_price ? Mark::clean_price(*p.clean_price) : Mark::curve(p.z_spread);
            PositionValuation v;
            v.id = p.id;
            v.face = p.face;
            v.bond = value_bond(p.bond, settle, mark, curve.get());
            const CurveRisk risk = curve_risk(p.bond, settle, curve, *v.bond.z_spread, key_tenors);
            const double scale = p.face / 100.0;
            v.market_value = scale * v.bond.dirty;
            v.accrued_value = scale * v.bond.accrued;
            v.dv01 = scale * risk.dv01;
            v.effective_duration = risk.effective_duration;
            v.effective_convexity = risk.effective_convexity;
            for (std::size_t k = 0; k < key_tenors.size(); ++k) {
                v.key_dv01.push_back(scale * risk.key_dv01[k]);
                out.key_dv01[k] += v.key_dv01[k];
            }
            out.face += p.face;
            out.market_value += v.market_value;
            out.accrued_value += v.accrued_value;
            out.dv01 += v.dv01;
            weighted_convexity += v.market_value * v.effective_convexity;
            weighted_yield += v.market_value * v.bond.yield;
            out.positions.push_back(std::move(v));
        } catch (const Error& e) {
            throw Error("position '" + p.id + "': " + e.what());
        }
    }

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const bool has_value = out.market_value != 0.0;
    out.duration = has_value ? out.dv01 * 1e4 / out.market_value : nan;
    out.convexity = has_value ? weighted_convexity / out.market_value : nan;
    out.yield = has_value ? weighted_yield / out.market_value : nan;
    return out;
}

double shocked_market_value(const std::vector<Position>& positions,
                            const PortfolioValuation& valuation, const CurvePtr& curve,
                            const ZeroShift& shift) {
    require(positions.size() == valuation.positions.size(),
            "valuation does not match the positions");
    const CurvePtr shocked = shifted(curve, shift);
    double total = 0.0;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const double z = *valuation.positions[i].bond.z_spread;
        total += positions[i].face / 100.0 *
                 dirty_price_from_curve(positions[i].bond, valuation.settle, *shocked, z);
    }
    return total;
}

std::vector<DatedFlow> portfolio_cashflows(const std::vector<Position>& positions, Date settle) {
    std::map<Date, DatedFlow> by_date;
    for (const Position& p : positions) {
        const auto& flows = p.bond.cashflows();
        for (std::size_t i = p.bond.first_cashflow_after(settle); i < flows.size(); ++i) {
            DatedFlow& f = by_date[flows[i].payment];
            f.payment = flows[i].payment;
            f.coupon += p.face / 100.0 * flows[i].coupon;
            f.principal += p.face / 100.0 * flows[i].principal;
        }
    }
    std::vector<DatedFlow> out;
    for (const auto& [date, flow] : by_date) {
        out.push_back(flow);
    }
    return out;
}

} // namespace fit

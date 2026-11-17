#pragma once

#include "fit/bond/analytics.hpp"
#include "fit/bond/bond.hpp"
#include "fit/curve/yield_curve.hpp"

#include <optional>
#include <string>
#include <vector>

namespace fit {

/// A holding (or proposed trade) in one bond. `face` is signed: positive long / buy.
struct Position {
    std::string id;
    double face = 0.0;
    Bond bond;
    std::optional<double> clean_price; ///< market clean price; marks the position when set
    double z_spread = 0.0;             ///< decimal; used when no price is given
};

/// Reads positions from CSV. Required columns: `id,face,maturity,coupon`; optional:
/// `frequency` (2), `daycount` (act/act), `issue`, `price` (clean, per 100) and `zspread`
/// (bp). `coupon` is in percent. A row may carry `price` or `zspread`, not both.
std::vector<Position> load_positions(const std::string& path, const Calendar& calendar);

/// Valuation and risk of one position in currency units.
struct PositionValuation {
    std::string id;
    double face = 0.0;
    BondValuation bond;         ///< per-100 analytics
    double market_value = 0.0;  ///< face * dirty / 100
    double accrued_value = 0.0; ///< face * accrued / 100
    double dv01 = 0.0;          ///< currency P&L for a 1bp parallel fall in zero rates
    double effective_duration = 0.0;
    double effective_convexity = 0.0;
    std::vector<double> key_dv01; ///< currency, aligned with the portfolio key tenors
};

struct PortfolioValuation {
    Date settle;
    std::vector<PositionValuation> positions;
    std::vector<double> key_tenors;

    double face = 0.0;
    double market_value = 0.0;
    double accrued_value = 0.0;
    double dv01 = 0.0;
    std::vector<double> key_dv01;
    // Ratios are NaN when the net market value is zero.
    double duration = 0.0;  ///< dv01 * 10000 / market_value
    double convexity = 0.0; ///< market-value weighted effective convexity
    double yield = 0.0;     ///< market-value weighted yield to maturity
};

/// Marks every position at `settle` (market price when supplied, otherwise the curve plus
/// the position's z-spread) and aggregates value and curve risk.
PortfolioValuation value_portfolio(const std::vector<Position>& positions, Date settle,
                                   const CurvePtr& curve,
                                   const std::vector<double>& key_tenors = default_key_tenors());

/// Market value of an already valued portfolio after an instantaneous zero-rate shift, with
/// each position's z-spread held constant.
double shocked_market_value(const std::vector<Position>& positions,
                            const PortfolioValuation& valuation, const CurvePtr& curve,
                            const ZeroShift& shift);

struct DatedFlow {
    Date payment;
    double coupon = 0.0;
    double principal = 0.0;
};

/// Projected cashflows after `settle`, aggregated by payment date, in currency units.
std::vector<DatedFlow> portfolio_cashflows(const std::vector<Position>& positions, Date settle);

} // namespace fit

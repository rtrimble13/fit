#pragma once

#include "fit/bond/bond.hpp"
#include "fit/curve/yield_curve.hpp"

#include <optional>
#include <vector>

namespace fit {

// All prices are per 100 face; yields, spreads and shifts are decimals.

// ---- Yield-based analytics (street convention) ------------------------------------------------
//
// Cashflows are discounted at (1 + y/f)^(-f * t), with t measured in the bond's day count
// along nominal (unadjusted) coupon dates, so t is a whole number of periods plus the
// fraction of the current period remaining at settlement.

double dirty_price_from_yield(const Bond& bond, Date settle, double yield);
double clean_price_from_yield(const Bond& bond, Date settle, double yield);
double yield_from_clean_price(const Bond& bond, Date settle, double clean_price);
double yield_from_dirty_price(const Bond& bond, Date settle, double dirty_price);

struct YieldRisk {
    double macaulay_duration = 0.0; ///< years
    double modified_duration = 0.0; ///< -dP/dy / P
    double convexity = 0.0;         ///< d2P/dy2 / P
    double dv01 = 0.0;              ///< price change per 100 face for a 1bp fall in yield
};

YieldRisk yield_risk(const Bond& bond, Date settle, double yield);

// ---- Curve-based analytics ---------------------------------------------------------------------
//
// Cashflows with a nominal coupon date after settlement are discounted on the curve, from
// their payment dates back to the settlement date, plus a continuously
// compounded z-spread: PV = sum CF_i * DF(t_i) / DF(t_s) * exp(-z * (t_i - t_s)).

double dirty_price_from_curve(const Bond& bond, Date settle, const YieldCurve& curve,
                              double z_spread = 0.0);
double clean_price_from_curve(const Bond& bond, Date settle, const YieldCurve& curve,
                              double z_spread = 0.0);
double z_spread_from_dirty_price(const Bond& bond, Date settle, const YieldCurve& curve,
                                 double dirty_price);

/// Standard key-rate tenors in years: 0.25, 0.5, 1, 2, 3, 5, 7, 10, 20, 30.
const std::vector<double>& default_key_tenors();

struct CurveRisk {
    double dirty = 0.0;
    double dv01 = 0.0; ///< price change per 100 face for a 1bp parallel fall
    double effective_duration = 0.0;
    double effective_convexity = 0.0;
    std::vector<double> key_tenors; ///< years
    std::vector<double> key_dv01;   ///< per 100 face, sums to (approximately) dv01
};

/// Bump-and-reprice sensitivities to zero-rate shifts at constant z-spread (ADR-0009).
/// `bump` is the shift size (default 1bp), applied up and down for central differences.
CurveRisk curve_risk(const Bond& bond, Date settle, const CurvePtr& curve, double z_spread,
                     const std::vector<double>& key_tenors = default_key_tenors(),
                     double bump = 1e-4);

// ---- One-stop valuation ------------------------------------------------------------------------

struct BondValuation {
    Date settle;
    double clean = 0.0;
    double dirty = 0.0;
    double accrued = 0.0;
    double yield = 0.0;
    YieldRisk yield_risk;
    std::optional<double> z_spread; ///< set when a curve was supplied
};

/// How a bond is marked.
struct Mark {
    enum class Kind { CleanPrice, Yield, Curve };
    Kind kind = Kind::Curve;
    double value = 0.0; ///< clean price, yield, or z-spread over the curve

    static Mark clean_price(double price) { return {Kind::CleanPrice, price}; }
    static Mark yield(double y) { return {Kind::Yield, y}; }
    static Mark curve(double z_spread = 0.0) { return {Kind::Curve, z_spread}; }
};

/// Values `bond` at `settle`. A curve is required for `Mark::Kind::Curve`; when a curve is
/// supplied with a price or yield mark, the implied z-spread is solved for.
BondValuation value_bond(const Bond& bond, Date settle, const Mark& mark,
                         const YieldCurve* curve = nullptr);

} // namespace fit

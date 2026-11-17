#pragma once

#include "fit/bond/analytics.hpp"
#include "fit/bond/bond.hpp"
#include "fit/curve/yield_curve.hpp"

#include <optional>

namespace fit {

/// What the curve is assumed to look like on the horizon date, before any shift.
enum class HorizonBase {
    Unchanged, ///< zero rates unchanged tenor-by-tenor: the bond rolls down the curve
    Forward    ///< today's forward rates are realised
};

HorizonBase parse_horizon_base(std::string_view text);
std::string to_string(HorizonBase base);

struct HorizonSpec {
    Date horizon; ///< horizon settlement date
    HorizonBase base = HorizonBase::Unchanged;
    ZeroShift shift;                     ///< zero-rate shock applied to the horizon curve
    std::optional<double> reinvest_rate; ///< decimal, compounded at the coupon frequency;
                                         ///< default: forward rates implied by today's curve
};

/// Horizon (total return) analysis of one bond, per 100 face.
struct HorizonResult {
    Date settle;
    Date horizon;
    double z_spread = 0.0; ///< held constant to the horizon

    double clean_start = 0.0;
    double accrued_start = 0.0;
    double dirty_start = 0.0;
    double yield_start = 0.0;

    double clean_end = 0.0;
    double accrued_end = 0.0;
    double dirty_end = 0.0;
    double yield_end = 0.0;

    double coupons = 0.0; ///< coupons with nominal date in (settle, horizon]

    // P&L attribution; the four components sum to `total_pnl`.
    double coupon_income = 0.0; ///< coupons received plus change in accrued interest
    double roll = 0.0;          ///< clean price change on the unshifted horizon curve
    double curve_shift = 0.0;   ///< clean price change due to the shift
    double reinvestment = 0.0;  ///< interest earned on coupons to the horizon
    double total_pnl = 0.0;

    double total_return = 0.0;      ///< total_pnl / dirty_start
    double annualized_return = 0.0; ///< (1 + total_return)^(365/days) - 1
};

/// Projects `bond` from `settle` to `spec.horizon`. The bond is marked with `mark` today and
/// revalued on the horizon curve at the same z-spread.
HorizonResult horizon_analysis(const Bond& bond, Date settle, const Mark& mark,
                               const CurvePtr& curve, const HorizonSpec& spec);

} // namespace fit

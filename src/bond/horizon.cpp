#include "fit/bond/horizon.hpp"

#include "fit/core/error.hpp"
#include "fit/io/csv.hpp"

#include <cmath>
#include <memory>

namespace fit {

HorizonBase parse_horizon_base(std::string_view text) {
    const std::string t = to_lower(text);
    if (t == "unchanged" || t == "rolldown" || t == "roll-down") {
        return HorizonBase::Unchanged;
    }
    if (t == "forward" || t == "forwards") {
        return HorizonBase::Forward;
    }
    throw Error("unknown horizon scenario '" + std::string(text) +
                "' (expected unchanged or forward)");
}

std::string to_string(HorizonBase base) {
    return base == HorizonBase::Unchanged ? "unchanged" : "forward";
}

HorizonResult horizon_analysis(const Bond& bond, Date settle, const Mark& mark,
                               const CurvePtr& curve, const HorizonSpec& spec) {
    require(curve != nullptr, "horizon analysis requires a curve");
    require(spec.horizon > settle, "horizon " + spec.horizon.to_string() +
                                       " must be after settlement " + settle.to_string());
    require(spec.horizon < bond.maturity(), "horizon " + spec.horizon.to_string() +
                                                " must be before maturity " +
                                                bond.maturity().to_string());

    const BondValuation start = value_bond(bond, settle, mark, curve.get());

    HorizonResult r;
    r.settle = settle;
    r.horizon = spec.horizon;
    r.z_spread = *start.z_spread;
    r.clean_start = start.clean;
    r.accrued_start = start.accrued;
    r.dirty_start = start.dirty;
    r.yield_start = start.yield;

    // The horizon curve keeps today's lag between curve date and settlement date.
    const Date horizon_as_of = spec.horizon - (settle - curve->as_of());
    CurvePtr base;
    if (spec.base == HorizonBase::Unchanged) {
        base = std::make_shared<RebasedCurve>(curve, horizon_as_of);
    } else {
        base = std::make_shared<ForwardImpliedCurve>(curve, horizon_as_of);
    }
    const double dirty_base = dirty_price_from_curve(bond, spec.horizon, *base, r.z_spread);
    r.dirty_end =
        dirty_price_from_curve(bond, spec.horizon, *shifted(base, spec.shift), r.z_spread);
    r.accrued_end = bond.accrued(spec.horizon);
    r.clean_end = r.dirty_end - r.accrued_end;
    r.yield_end = yield_from_dirty_price(bond, spec.horizon, r.dirty_end);

    // Coupons paid after settlement up to and including the horizon, reinvested to it.
    const int f = bond.spec().frequency;
    for (std::size_t i = bond.first_cashflow_after(settle); i < bond.cashflows().size(); ++i) {
        const Cashflow& cf = bond.cashflows()[i];
        if (cf.payment > spec.horizon) {
            break;
        }
        double growth;
        if (spec.reinvest_rate) {
            const double years = (spec.horizon - cf.payment) / 365.0;
            growth = std::pow(1.0 + *spec.reinvest_rate / f, f * years);
        } else {
            growth = curve->discount(cf.payment) / curve->discount(spec.horizon);
        }
        r.coupons += cf.amount();
        r.reinvestment += cf.amount() * (growth - 1.0);
    }

    r.coupon_income = r.coupons + r.accrued_end - r.accrued_start;
    r.roll = (dirty_base - r.accrued_end) - r.clean_start;
    r.curve_shift = r.dirty_end - dirty_base;
    r.total_pnl = r.dirty_end + r.coupons + r.reinvestment - r.dirty_start;
    r.total_return = r.total_pnl / r.dirty_start;
    r.annualized_return = std::pow(1.0 + r.total_return, 365.0 / (spec.horizon - settle)) - 1.0;
    return r;
}

} // namespace fit

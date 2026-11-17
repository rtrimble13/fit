#include "fit/bond/analytics.hpp"

#include "fit/core/error.hpp"
#include "fit/math/solver.hpp"

#include <cmath>

namespace fit {

namespace {

constexpr double kMinYield = -0.5;
constexpr double kMaxYield = 10.0;
constexpr double kMaxSpread = 10.0;

struct TimedFlow {
    double time; ///< years from settlement in the bond's day count (street convention)
    double amount;
};

/// Remaining cashflows with street-convention discounting times.
std::vector<TimedFlow> timed_flows(const Bond& bond, Date settle) {
    const BondSpec& spec = bond.spec();
    const auto& periods = bond.periods();
    const auto& flows = bond.cashflows();
    std::vector<TimedFlow> out;
    double t = 0.0;
    bool started = false;
    for (std::size_t i = 0; i < periods.size(); ++i) {
        const SchedulePeriod& p = periods[i];
        if (p.accrual_end <= settle) {
            continue;
        }
        const double full = year_fraction(spec.day_count, p.accrual_start, p.accrual_end,
                                          p.ref_start, p.ref_end, spec.frequency);
        if (!started) {
            started = true;
            const Date from = std::max(settle, p.accrual_start);
            const double elapsed = year_fraction(spec.day_count, p.accrual_start, from, p.ref_start,
                                                 p.ref_end, spec.frequency);
            // Settlement before the dated date: add the wait until accrual starts.
            const double wait = settle < p.accrual_start ? (p.accrual_start - settle) / 365.0 : 0.0;
            t = full - elapsed + wait;
        } else {
            t += full;
        }
        out.push_back({t, flows[i].amount()});
    }
    require(!out.empty(), "bond" + (spec.id.empty() ? std::string() : " '" + spec.id + "'") +
                              " has no cashflows after " + settle.to_string() + " (matures " +
                              spec.maturity.to_string() + ")");
    return out;
}

double pv_at_yield(const std::vector<TimedFlow>& flows, int frequency, double yield) {
    const double base = 1.0 + yield / frequency;
    double pv = 0.0;
    for (const TimedFlow& f : flows) {
        pv += f.amount * std::pow(base, -frequency * f.time);
    }
    return pv;
}

} // namespace

double dirty_price_from_yield(const Bond& bond, Date settle, double yield) {
    require(yield > -bond.spec().frequency, "yield is below -100% per period");
    return pv_at_yield(timed_flows(bond, settle), bond.spec().frequency, yield);
}

double clean_price_from_yield(const Bond& bond, Date settle, double yield) {
    return dirty_price_from_yield(bond, settle, yield) - bond.accrued(settle);
}

double yield_from_dirty_price(const Bond& bond, Date settle, double dirty_price) {
    require(dirty_price > 0.0, "price must be positive");
    const auto flows = timed_flows(bond, settle);
    const int f = bond.spec().frequency;
    return solve_near([&](double y) { return pv_at_yield(flows, f, y) - dirty_price; },
                      bond.spec().coupon, 0.05, kMinYield, kMaxYield, "yield");
}

double yield_from_clean_price(const Bond& bond, Date settle, double clean_price) {
    return yield_from_dirty_price(bond, settle, clean_price + bond.accrued(settle));
}

YieldRisk yield_risk(const Bond& bond, Date settle, double yield) {
    const auto flows = timed_flows(bond, settle);
    const int f = bond.spec().frequency;
    const double base = 1.0 + yield / f;
    double pv = 0.0;
    double weighted_time = 0.0;
    double weighted_convexity = 0.0;
    for (const TimedFlow& flow : flows) {
        const double value = flow.amount * std::pow(base, -f * flow.time);
        pv += value;
        weighted_time += flow.time * value;
        weighted_convexity += flow.time * (flow.time + 1.0 / f) * value;
    }
    YieldRisk risk;
    risk.macaulay_duration = weighted_time / pv;
    risk.modified_duration = risk.macaulay_duration / base;
    risk.convexity = weighted_convexity / (pv * base * base);
    risk.dv01 = risk.modified_duration * pv * 1e-4;
    return risk;
}

double dirty_price_from_curve(const Bond& bond, Date settle, const YieldCurve& curve,
                              double z_spread) {
    const auto& flows = bond.cashflows();
    const double settle_df = curve.discount(settle);
    const double settle_time = curve.time(settle);
    double pv = 0.0;
    for (std::size_t i = bond.first_cashflow_after(settle); i < flows.size(); ++i) {
        const double t = curve.time(flows[i].payment);
        pv += flows[i].amount() * curve.discount_t(t) * std::exp(-z_spread * (t - settle_time));
    }
    return pv / settle_df;
}

double clean_price_from_curve(const Bond& bond, Date settle, const YieldCurve& curve,
                              double z_spread) {
    return dirty_price_from_curve(bond, settle, curve, z_spread) - bond.accrued(settle);
}

double z_spread_from_dirty_price(const Bond& bond, Date settle, const YieldCurve& curve,
                                 double dirty_price) {
    require(dirty_price > 0.0, "price must be positive");
    return solve_near(
        [&](double z) { return dirty_price_from_curve(bond, settle, curve, z) - dirty_price; }, 0.0,
        0.01, -kMaxSpread, kMaxSpread, "z-spread");
}

const std::vector<double>& default_key_tenors() {
    static const std::vector<double> tenors{0.25, 0.5, 1.0, 2.0, 3.0, 5.0, 7.0, 10.0, 20.0, 30.0};
    return tenors;
}

CurveRisk curve_risk(const Bond& bond, Date settle, const CurvePtr& curve, double z_spread,
                     const std::vector<double>& key_tenors, double bump) {
    require(bump > 0.0, "bump size must be positive");
    const auto price = [&](const ZeroShift& shift) {
        return dirty_price_from_curve(bond, settle, *shifted(curve, shift), z_spread);
    };
    // Scale a price difference for a +/- `bump` shift to a per-basis-point sensitivity.
    const double per_bp = 1e-4 / (2.0 * bump);

    CurveRisk risk;
    risk.dirty = price(ZeroShift{});
    const double up = price(ZeroShift::parallel(bump));
    const double down = price(ZeroShift::parallel(-bump));
    risk.dv01 = (down - up) * per_bp;
    risk.effective_duration = (down - up) / (2.0 * bump * risk.dirty);
    risk.effective_convexity = (up + down - 2.0 * risk.dirty) / (bump * bump * risk.dirty);
    risk.key_tenors = key_tenors;
    for (std::size_t k = 0; k < key_tenors.size(); ++k) {
        const double key_up = price(ZeroShift::key_rate(key_tenors, k, bump));
        const double key_down = price(ZeroShift::key_rate(key_tenors, k, -bump));
        risk.key_dv01.push_back((key_down - key_up) * per_bp);
    }
    return risk;
}

BondValuation value_bond(const Bond& bond, Date settle, const Mark& mark, const YieldCurve* curve) {
    BondValuation v;
    v.settle = settle;
    v.accrued = bond.accrued(settle);
    switch (mark.kind) {
    case Mark::Kind::CleanPrice:
        v.clean = mark.value;
        v.dirty = v.clean + v.accrued;
        v.yield = yield_from_dirty_price(bond, settle, v.dirty);
        break;
    case Mark::Kind::Yield:
        v.yield = mark.value;
        v.dirty = dirty_price_from_yield(bond, settle, v.yield);
        v.clean = v.dirty - v.accrued;
        break;
    case Mark::Kind::Curve:
        require(curve != nullptr, "a curve is required to price off the curve");
        v.dirty = dirty_price_from_curve(bond, settle, *curve, mark.value);
        v.clean = v.dirty - v.accrued;
        v.yield = yield_from_dirty_price(bond, settle, v.dirty);
        v.z_spread = mark.value;
        break;
    }
    if (curve != nullptr && !v.z_spread) {
        v.z_spread = z_spread_from_dirty_price(bond, settle, *curve, v.dirty);
    }
    v.yield_risk = yield_risk(bond, settle, v.yield);
    return v;
}

} // namespace fit

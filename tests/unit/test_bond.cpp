#include "fit/bond/analytics.hpp"
#include "fit/bond/bond.hpp"
#include "fit/bond/horizon.hpp"
#include "fit/core/error.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <memory>
#include <optional>

using namespace fit;

namespace {

Bond make_bond(std::optional<Date> issue, Date maturity, double coupon_pct, int frequency,
               DayCount day_count, Calendar calendar = Calendar(Calendar::Rules::None)) {
    BondSpec spec;
    spec.issue = issue;
    spec.maturity = maturity;
    spec.coupon = coupon_pct / 100.0;
    spec.frequency = frequency;
    spec.day_count = day_count;
    spec.calendar = calendar;
    return Bond(spec);
}

CurvePtr flat_curve(Date as_of, double rate) {
    return std::make_shared<InterpolatedCurve>(as_of, std::vector<double>{1.0, 50.0},
                                               std::vector<double>{rate, rate},
                                               InterpolationMethod::FlatForward);
}

const Date kAsOf(2026, 11, 17);
const Date kSettle(2026, 11, 18);

} // namespace

TEST_CASE("bond: published Microsoft Excel reference values") {
    // PRICE(2008-02-15, 2017-11-15, 5.75%, 6.5%, 100, 2, 0) = 94.63436
    const Bond a = make_bond({}, Date(2017, 11, 15), 5.75, 2, DayCount::Thirty360US);
    CHECK(clean_price_from_yield(a, Date(2008, 2, 15), 0.065) ==
          doctest::Approx(94.63436).epsilon(1e-7));
    // YIELD(2008-02-15, 2016-11-15, 5.75%, 95.04287, 100, 2, 0) = 6.5%
    const Bond b = make_bond({}, Date(2016, 11, 15), 5.75, 2, DayCount::Thirty360US);
    CHECK(yield_from_clean_price(b, Date(2008, 2, 15), 95.04287) ==
          doctest::Approx(0.065).epsilon(1e-6));
    // DURATION(2018-07-01, 2048-01-01, 8%, 9%, 2, 1) = 10.9191453
    const Bond c = make_bond({}, Date(2048, 1, 1), 8.0, 2, DayCount::ActActICMA);
    CHECK(yield_risk(c, Date(2018, 7, 1), 0.09).macaulay_duration ==
          doctest::Approx(10.9191453).epsilon(1e-8));
    // MDURATION(2008-01-01, 2016-01-01, 8%, 9%, 2, 1) = 5.736
    const Bond d = make_bond({}, Date(2016, 1, 1), 8.0, 2, DayCount::ActActICMA);
    CHECK(yield_risk(d, Date(2008, 1, 1), 0.09).modified_duration ==
          doctest::Approx(5.736).epsilon(1e-4));
}

TEST_CASE("bond: analytics match QuantLib across conventions") {
    // Reference values from QuantLib BondFunctions (see tools/quantlib_crosscheck.py).
    struct Case {
        Date issue, maturity;
        double coupon;
        int frequency;
        DayCount day_count;
        Date settle;
        double yield, accrued, clean, modified, macaulay, convexity;
    };
    const Case cases[] = {
        // regular Treasury-style bond
        {Date(2016, 11, 15), Date(2036, 11, 15), 4.25, 2, DayCount::ActActICMA, Date(2026, 11, 18),
         0.044, 0.0352209945, 98.7974063813, 8.0513872508, 8.2285177704, 77.1345398625},
        // 30/360 with settlement inside a short first stub
        {Date(2026, 1, 10), Date(2031, 11, 15), 5.5, 2, DayCount::Thirty360US, Date(2026, 3, 3),
         0.061, 0.8097222222, 97.1475658883, 4.7703312312, 4.9158263338, 27.3319663775},
        // end-of-month schedule
        {Date(2025, 3, 31), Date(2030, 8, 31), 3.0, 2, DayCount::ActActICMA, Date(2026, 6, 10),
         0.022, 0.8315217391, 103.2072451640, 3.9278142760, 3.9710202330, 18.0114472831},
        // quarterly 30E/360 rolling on the 28th/29th of February
        {Date(2024, 2, 29), Date(2034, 2, 28), 6.0, 4, DayCount::ThirtyE360, Date(2027, 1, 20),
         0.073, 0.8333333333, 92.8393311720, 5.6248434832, 5.7274968768, 37.7569635808},
        // annual ACT/ACT with a short first stub
        {Date(2026, 1, 10), Date(2031, 11, 15), 5.5, 1, DayCount::ActActICMA, Date(2026, 3, 3),
         0.061, 0.7835616438, 97.1813893443, 4.7096891840, 4.9969802242, 28.4900836812},
        // ACT/365F: coupon amounts vary with the day count
        {Date(2021, 5, 20), Date(2046, 5, 20), 2.75, 2, DayCount::Act365F, Date(2026, 9, 1), 0.049,
         0.7835616438, 73.0016492473, 13.9574037225, 14.2993601137, 246.7488857890},
    };
    for (const Case& c : cases) {
        CAPTURE(c.maturity.to_string());
        const Bond bond = make_bond(c.issue, c.maturity, c.coupon, c.frequency, c.day_count);
        CHECK(bond.accrued(c.settle) == doctest::Approx(c.accrued).epsilon(1e-9));
        CHECK(clean_price_from_yield(bond, c.settle, c.yield) ==
              doctest::Approx(c.clean).epsilon(1e-10));
        const YieldRisk risk = yield_risk(bond, c.settle, c.yield);
        CHECK(risk.modified_duration == doctest::Approx(c.modified).epsilon(1e-9));
        CHECK(risk.macaulay_duration == doctest::Approx(c.macaulay).epsilon(1e-9));
        CHECK(risk.convexity == doctest::Approx(c.convexity).epsilon(1e-9));
        // price <-> yield round trip
        CHECK(yield_from_clean_price(bond, c.settle, c.clean) ==
              doctest::Approx(c.yield).epsilon(1e-9));
    }
}

TEST_CASE("bond: a par bond on a coupon date prices at 100 and DV01 matches a bump") {
    const Bond bond = make_bond({}, Date(2036, 11, 15), 4.25, 2, DayCount::ActActICMA);
    const Date settle(2026, 11, 15);
    CHECK(bond.accrued(settle) == 0.0);
    CHECK(dirty_price_from_yield(bond, settle, 0.0425) == doctest::Approx(100.0).epsilon(1e-12));
    const YieldRisk risk = yield_risk(bond, settle, 0.0425);
    const double bumped = (dirty_price_from_yield(bond, settle, 0.0425 - 0.5e-4) -
                           dirty_price_from_yield(bond, settle, 0.0425 + 0.5e-4));
    CHECK(risk.dv01 == doctest::Approx(bumped).epsilon(1e-6));
}

TEST_CASE("bond: zero-coupon bond") {
    const Bond bond = make_bond({}, Date(2031, 11, 18), 0.0, 2, DayCount::ActActICMA);
    const Date settle(2026, 11, 18);
    CHECK(bond.accrued(settle) == 0.0);
    CHECK(dirty_price_from_yield(bond, settle, 0.04) ==
          doctest::Approx(100.0 / std::pow(1.02, 10)).epsilon(1e-12));
    CHECK(yield_risk(bond, settle, 0.04).macaulay_duration == doctest::Approx(5.0).epsilon(1e-12));

    // On a flat continuously compounded curve the price and duration are closed-form.
    const CurvePtr curve = flat_curve(settle, 0.04);
    const double t = (Date(2031, 11, 18) - settle) / 365.0;
    CHECK(dirty_price_from_curve(bond, settle, *curve) ==
          doctest::Approx(100.0 * std::exp(-0.04 * t)).epsilon(1e-13));
    CHECK(curve_risk(bond, settle, curve, 0.0).effective_duration ==
          doctest::Approx(t).epsilon(1e-7));
}

TEST_CASE("bond: cashflows and payment-date adjustment") {
    const Bond bond = make_bond({}, Date(2028, 11, 15), 4.0, 2, DayCount::ActActICMA,
                                Calendar(Calendar::Rules::WeekendsOnly));
    const std::size_t first = bond.first_cashflow_after(kSettle);
    const auto& flows = bond.cashflows();
    REQUIRE(flows.size() - first == 4);
    CHECK(flows[first].accrual_end == Date(2027, 5, 15)); // a Saturday
    CHECK(flows[first].payment == Date(2027, 5, 17));
    CHECK(flows[first].coupon == doctest::Approx(2.0));
    CHECK(flows.back().principal == 100.0);
    CHECK(flows.back().amount() == doctest::Approx(102.0));
    CHECK_THROWS_AS(bond.first_cashflow_after(Date(2028, 11, 15)), Error);
}

TEST_CASE("bond: yield and curve pricing agree on which cashflows remain") {
    // The 15 May 2027 coupon date is a Saturday, paid on Monday the 17th. A buyer settling
    // on the Sunday in between does not receive it, under either pricing route.
    const Bond bond = make_bond({}, Date(2029, 11, 15), 4.25, 2, DayCount::ActActICMA,
                                Calendar(Calendar::Rules::WeekendsOnly));
    const Date sunday(2027, 5, 16);
    const CurvePtr curve = flat_curve(kAsOf, 0.04);
    CHECK(bond.cashflows()[bond.first_cashflow_after(sunday)].payment == Date(2027, 11, 15));
    const BondValuation v = value_bond(bond, sunday, Mark::curve(), curve.get());
    // Semi-annual equivalent of a 4% continuous rate is 4.04%; a wrong cashflow set would
    // put the yield far from the curve.
    CHECK(v.yield == doctest::Approx(0.0404).epsilon(2e-3));
    CHECK(v.accrued == doctest::Approx(100.0 * 0.0425 * 1.0 / (2.0 * 184.0)).epsilon(1e-12));
}

TEST_CASE("bond: z-spread round trip and consistency of marks") {
    const CurvePtr curve = std::make_shared<InterpolatedCurve>(
        kAsOf, std::vector<double>{1.0, 5.0, 10.0, 30.0},
        std::vector<double>{0.040, 0.039, 0.042, 0.046}, InterpolationMethod::MonotoneCubic);
    const Bond bond = make_bond({}, Date(2036, 11, 15), 4.25, 2, DayCount::ActActICMA);

    const double dirty = dirty_price_from_curve(bond, kSettle, *curve, 0.0075);
    CHECK(z_spread_from_dirty_price(bond, kSettle, *curve, dirty) ==
          doctest::Approx(0.0075).epsilon(1e-10));
    CHECK(dirty < dirty_price_from_curve(bond, kSettle, *curve, 0.0));

    // The three ways of marking the same bond agree with each other.
    const BondValuation by_curve = value_bond(bond, kSettle, Mark::curve(0.0075), curve.get());
    const BondValuation by_price =
        value_bond(bond, kSettle, Mark::clean_price(by_curve.clean), curve.get());
    const BondValuation by_yield =
        value_bond(bond, kSettle, Mark::yield(by_curve.yield), curve.get());
    CHECK(by_price.yield == doctest::Approx(by_curve.yield).epsilon(1e-11));
    CHECK(*by_price.z_spread == doctest::Approx(0.0075).epsilon(1e-9));
    CHECK(by_yield.clean == doctest::Approx(by_curve.clean).epsilon(1e-11));
    CHECK(by_curve.dirty == doctest::Approx(by_curve.clean + by_curve.accrued));
    CHECK_THROWS_AS(value_bond(bond, kSettle, Mark::curve()), Error); // no curve supplied
}

TEST_CASE("bond: key-rate DV01s sum to the parallel DV01") {
    const CurvePtr curve = std::make_shared<InterpolatedCurve>(
        kAsOf, std::vector<double>{1.0, 5.0, 10.0, 30.0},
        std::vector<double>{0.040, 0.039, 0.042, 0.046}, InterpolationMethod::LinearZero);
    const Bond bond = make_bond({}, Date(2046, 5, 15), 4.5, 2, DayCount::ActActICMA);
    const CurveRisk risk = curve_risk(bond, kSettle, curve, 0.003);
    double sum = 0.0;
    for (const double k : risk.key_dv01) {
        sum += k;
    }
    CHECK(sum == doctest::Approx(risk.dv01).epsilon(1e-6));
    CHECK(risk.dv01 > 0.0);
    CHECK(risk.effective_convexity > 0.0);
    CHECK(risk.effective_duration == doctest::Approx(risk.dv01 * 1e4 / risk.dirty).epsilon(1e-9));
    // A 20-year bond has most of its risk at the 20Y key and none at 30Y... except for
    // the triangular overlap, which is strictly less than the 20Y bucket.
    CHECK(risk.key_dv01[8] > risk.key_dv01[9]);
}

TEST_CASE("bond: invalid terms are rejected") {
    CHECK_THROWS_AS(make_bond({}, Date(2030, 1, 1), -1.0, 2, DayCount::ActActICMA), Error);
    CHECK_THROWS_AS(make_bond({}, Date(2030, 1, 1), 4.0, 3, DayCount::ActActICMA), Error);
    CHECK_THROWS_AS(make_bond(Date(2031, 1, 1), Date(2030, 1, 1), 4.0, 2, DayCount::ActActICMA),
                    Error);
    const Bond bond = make_bond({}, Date(2030, 1, 1), 4.0, 2, DayCount::ActActICMA);
    CHECK_THROWS_AS(dirty_price_from_yield(bond, Date(2030, 1, 1), 0.04), Error); // matured
    CHECK_THROWS_AS(yield_from_clean_price(bond, kSettle, -5.0), Error);
}

TEST_CASE("horizon: attribution components sum to total P&L") {
    const CurvePtr curve = std::make_shared<InterpolatedCurve>(
        kAsOf, std::vector<double>{1.0, 5.0, 10.0, 30.0},
        std::vector<double>{0.040, 0.039, 0.042, 0.046}, InterpolationMethod::FlatForward);
    const Bond bond = make_bond({}, Date(2036, 11, 15), 4.25, 2, DayCount::ActActICMA);
    for (const auto base : {HorizonBase::Unchanged, HorizonBase::Forward}) {
        for (const double shift : {-0.01, 0.0, 0.005}) {
            HorizonSpec spec;
            spec.horizon = Date(2028, 2, 18);
            spec.base = base;
            spec.shift = ZeroShift::parallel(shift);
            const HorizonResult r =
                horizon_analysis(bond, kSettle, Mark::clean_price(99.5), curve, spec);
            CHECK(r.coupon_income + r.roll + r.curve_shift + r.reinvestment ==
                  doctest::Approx(r.total_pnl).epsilon(1e-12));
            CHECK(r.total_pnl ==
                  doctest::Approx(r.dirty_end + r.coupons + r.reinvestment - r.dirty_start)
                      .epsilon(1e-12));
            CHECK(r.coupons == doctest::Approx(2.0 * 2.125)); // May-27 and Nov-27 coupons
            CHECK((r.curve_shift > 0.0) == (shift < 0.0));
            if (shift == 0.0) {
                CHECK(r.curve_shift == 0.0);
            }
        }
    }
}

TEST_CASE("horizon: if forwards are realised the bond earns the curve rate plus its spread") {
    // On a flat 4% continuously compounded curve with a constant z-spread s, the horizon
    // return over tau years is exactly exp((r + s) * tau) - 1 ... for the bond itself; the
    // reinvested coupons earn r only, so compare with a zero-coupon bond.
    const CurvePtr curve = flat_curve(kAsOf, 0.04);
    const Bond zero = make_bond({}, Date(2036, 11, 15), 0.0, 2, DayCount::ActActICMA);
    HorizonSpec spec;
    spec.horizon = Date(2028, 11, 18);
    spec.base = HorizonBase::Forward;
    const HorizonResult r = horizon_analysis(zero, kSettle, Mark::curve(0.01), curve, spec);
    const double tau = (spec.horizon - kSettle) / 365.0;
    CHECK(r.total_return == doctest::Approx(std::exp(0.05 * tau) - 1.0).epsilon(1e-12));
    CHECK(r.annualized_return == doctest::Approx(std::exp(0.05) - 1.0).epsilon(1e-12));

    // On a flat curve "unchanged" and "forward" are the same scenario.
    spec.base = HorizonBase::Unchanged;
    const HorizonResult u = horizon_analysis(zero, kSettle, Mark::curve(0.01), curve, spec);
    CHECK(u.total_return == doctest::Approx(r.total_return).epsilon(1e-12));

    // A coupon bond at zero spread earns exactly the curve rate when forwards are realised.
    const Bond coupon = make_bond({}, Date(2036, 11, 15), 4.25, 2, DayCount::ActActICMA);
    spec.base = HorizonBase::Forward;
    const HorizonResult c = horizon_analysis(coupon, kSettle, Mark::curve(0.0), curve, spec);
    CHECK(c.total_return == doctest::Approx(std::exp(0.04 * tau) - 1.0).epsilon(1e-12));
}

TEST_CASE("horizon: explicit reinvestment rate and input validation") {
    const CurvePtr curve = flat_curve(kAsOf, 0.04);
    const Bond bond = make_bond({}, Date(2036, 11, 15), 4.0, 2, DayCount::ActActICMA);
    HorizonSpec spec;
    spec.horizon = Date(2027, 11, 15);
    spec.reinvest_rate = 0.0;
    const HorizonResult r = horizon_analysis(bond, kSettle, Mark::curve(), curve, spec);
    CHECK(r.reinvestment == 0.0);
    CHECK(r.coupons == doctest::Approx(4.0)); // the coupon paid on the horizon date counts

    spec.reinvest_rate = 0.06;
    const HorizonResult s = horizon_analysis(bond, kSettle, Mark::curve(), curve, spec);
    const double years = (Date(2027, 11, 15) - Date(2027, 5, 15)) / 365.0;
    CHECK(s.reinvestment == doctest::Approx(2.0 * (std::pow(1.03, 2.0 * years) - 1.0)));

    spec.horizon = kSettle;
    CHECK_THROWS_AS(horizon_analysis(bond, kSettle, Mark::curve(), curve, spec), Error);
    spec.horizon = Date(2036, 11, 15);
    CHECK_THROWS_AS(horizon_analysis(bond, kSettle, Mark::curve(), curve, spec), Error);
    CHECK_THROWS_AS(parse_horizon_base("sideways"), Error);
}

#include "fit/core/error.hpp"
#include "fit/curve/bootstrap.hpp"
#include "fit/curve/yield_curve.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace fit;

namespace {

const Date kAsOf(2026, 11, 17);

std::vector<Quote> treasury_quotes() {
    return load_quotes(std::string(FIT_EXAMPLES_DIR) + "/usd_treasury.csv", kAsOf, {});
}

// The SOFR example as the walk-through builds it: T+2 on the US government bond calendar.
QuoteConventions sofr_conventions() {
    QuoteConventions conventions;
    conventions.calendar = Calendar(Calendar::Rules::UsGovernmentBond);
    conventions.settle_days = 2;
    return conventions;
}

std::vector<Quote> sofr_quotes() {
    return load_quotes(std::string(FIT_EXAMPLES_DIR) + "/usd_sofr.csv", kAsOf, sofr_conventions());
}

std::vector<Quote> term_sofr_quotes() {
    return load_quotes(std::string(FIT_EXAMPLES_DIR) + "/usd_term_sofr_3m.csv", kAsOf,
                       sofr_conventions());
}

// The example 3M Term SOFR projection curve, bootstrapped on the SOFR OIS curve.
BootstrapResult term_sofr_curve(InterpolationMethod method, CurvePtr* discount = nullptr) {
    const Calendar us = sofr_conventions().calendar;
    BootstrapOptions options{method};
    options.discount = bootstrap(kAsOf, sofr_quotes(), us, {method}).curve;
    if (discount != nullptr) {
        *discount = options.discount;
    }
    return bootstrap(kAsOf, term_sofr_quotes(), us, options);
}

const InterpolationMethod kAllMethods[] = {
    InterpolationMethod::LinearZero, InterpolationMethod::FlatForward,
    InterpolationMethod::CubicZero, InterpolationMethod::MonotoneCubic,
    InterpolationMethod::MonotoneConvex};

// Continuously compounded forward over [t - h, t] minus that over [t, t + h]: the jump in the
// instantaneous forward at t, up to O(h) for a continuous forward curve.
double forward_jump(const YieldCurve& curve, double t, double h) {
    const double left = std::log(curve.discount_t(t - h) / curve.discount_t(t)) / h;
    const double right = std::log(curve.discount_t(t) / curve.discount_t(t + h)) / h;
    return right - left;
}

} // namespace

TEST_CASE("curve: nodes are reproduced by every interpolation method") {
    const std::vector<double> times{0.5, 1.0, 2.0, 5.0, 10.0};
    const std::vector<double> zeros{0.040, 0.041, 0.043, 0.046, 0.048};
    for (const auto method : kAllMethods) {
        const InterpolatedCurve curve(kAsOf, times, zeros, method);
        CAPTURE(to_string(method));
        CHECK(curve.discount_t(0.0) == 1.0);
        for (std::size_t i = 0; i < times.size(); ++i) {
            CHECK(curve.zero_rate_t(times[i]) == doctest::Approx(zeros[i]).epsilon(1e-13));
        }
        // Before the first node the zero rate is held flat, except for the monotone schemes,
        // whose forward curves are smooth all the way down to t = 0.
        if (method != InterpolationMethod::MonotoneCubic &&
            method != InterpolationMethod::MonotoneConvex) {
            CHECK(curve.zero_rate_t(0.25) == doctest::Approx(0.040).epsilon(1e-12));
        }
        CHECK(curve.discount_t(0.25) < 1.0);
    }
}

TEST_CASE("curve: interpolation between nodes") {
    const std::vector<double> times{1.0, 2.0};
    const std::vector<double> zeros{0.03, 0.05};

    const InterpolatedCurve linear(kAsOf, times, zeros, InterpolationMethod::LinearZero);
    CHECK(linear.zero_rate_t(1.5) == doctest::Approx(0.04).epsilon(1e-13));
    CHECK(linear.zero_rate_t(7.0) == doctest::Approx(0.05).epsilon(1e-13)); // flat zero

    // Flat forward: -ln DF is linear, so the forward over [1, 2] is constant at 7%.
    const InterpolatedCurve flat(kAsOf, times, zeros, InterpolationMethod::FlatForward);
    CHECK(-std::log(flat.discount_t(1.5)) == doctest::Approx(0.03 + 0.5 * 0.07).epsilon(1e-13));
    CHECK(flat.instantaneous_forward_t(1.25) == doctest::Approx(0.07).epsilon(1e-10));
    CHECK(flat.instantaneous_forward_t(1.75) == doctest::Approx(0.07).epsilon(1e-10));
    CHECK(flat.instantaneous_forward_t(4.0) == doctest::Approx(0.07).epsilon(1e-10)); // extended

    // Monotone convex on -ln DF = (0, 0.03, 0.10) at t = (0, 1, 2): discrete forwards 3% and 7%,
    // node forwards f(1) = 5%, f(0) = 3% - (5% - 3%) / 2 = 2% and f(2) = 7% - (5% - 7%) / 2 = 8%.
    // [0, 1]: g0 = -1%, g1 = 2% = -2 g0, region (i): -ln DF(t) = 3% t - 1% (t - t^3).
    // [1, 2]: g0 = -2%, g1 = 1% = -g0 / 2, region (i): with u = t - 1,
    //         -ln DF = 0.03 + 7% u - 2% (u - 1.5 u^2 + 0.5 u^3), and f = 7% - 2% (1 - 3u + 1.5u^2).
    // Beyond t = 2 the forward stays at f(2) = 8%.
    const InterpolatedCurve convex(kAsOf, times, zeros, InterpolationMethod::MonotoneConvex);
    CHECK(convex.zero_rate_t(0.5) == doctest::Approx((0.015 - 0.01 * 0.375) / 0.5).epsilon(1e-13));
    CHECK(convex.zero_rate_t(1.5) ==
          doctest::Approx((0.03 + 0.035 - 0.02 * 0.1875) / 1.5).epsilon(1e-13));
    CHECK(convex.zero_rate_t(4.0) == doctest::Approx((0.10 + 0.08 * 2.0) / 4.0).epsilon(1e-13));
    // The one-day central difference of a quadratic forward is off by f'' h^2 / 6 = 1.9e-8
    // (f'' = -6%, h = 1/730).
    CHECK(convex.instantaneous_forward_t(1.5) == doctest::Approx(0.0725).epsilon(3e-8));
    CHECK(convex.instantaneous_forward_t(5.0) == doctest::Approx(0.08).epsilon(1e-10));
}

TEST_CASE("curve: monotone convex with one pillar is flat") {
    // One interval from the anchor: the forward is the single discrete forward, i.e. the zero.
    const InterpolatedCurve curve(kAsOf, {2.0}, {0.04}, InterpolationMethod::MonotoneConvex);
    for (const double t : {0.1, 1.0, 2.0, 7.5}) {
        CHECK(curve.zero_rate_t(t) == doctest::Approx(0.04).epsilon(1e-14));
    }
}

TEST_CASE("curve: monotone convex forwards stay positive where other schemes go negative") {
    // Zero rates whose discrete forwards are 6%, 0.5% and 6%: a sharp dip in the middle year.
    const std::vector<double> times{1.0, 2.0, 3.0};
    const std::vector<double> zeros{0.06, (0.06 + 0.005) / 2.0, (0.06 + 0.005 + 0.06) / 3.0};
    const InterpolatedCurve convex(kAsOf, times, zeros, InterpolationMethod::MonotoneConvex);
    const InterpolatedCurve spline(kAsOf, times, zeros, InterpolationMethod::CubicZero);
    double convex_min = 1.0;
    double spline_min = 1.0;
    for (int k = 1; k <= 500; ++k) {
        const double t = 0.01 * k;
        convex_min = std::min(convex_min, convex.instantaneous_forward_t(t));
        spline_min = std::min(spline_min, spline.instantaneous_forward_t(t));
    }
    // The bounded node forwards (1%) put the minimum at 0.25% mid-interval (see test_math.cpp).
    CHECK(convex_min == doctest::Approx(0.0025).epsilon(1e-6));
    CHECK(spline_min < 0.0); // the zero-rate spline overshoots into negative forwards
}

TEST_CASE("curve: monotone convex forwards are continuous at the pillars") {
    // On the bootstrapped Treasury curve the forward has no jump at any pillar, unlike
    // flat-forward, whose forwards are piecewise constant. With h = 1e-5 years the measured jump
    // of a continuous forward is O(|f'| h) plus rounding, far below 0.01bp.
    const BootstrapResult convex =
        bootstrap(kAsOf, treasury_quotes(), Calendar(), {InterpolationMethod::MonotoneConvex});
    const BootstrapResult flat =
        bootstrap(kAsOf, treasury_quotes(), Calendar(), {InterpolationMethod::FlatForward});
    double flat_max = 0.0;
    for (std::size_t i = 0; i < convex.quotes.size(); ++i) {
        const double t = convex.curve->times()[i];
        CAPTURE(t);
        CHECK(std::fabs(forward_jump(*convex.curve, t, 1e-5)) < 1e-6);
        flat_max = std::max(flat_max, std::fabs(forward_jump(*flat.curve, t, 1e-5)));
    }
    CHECK(flat_max > 1e-3); // the check can see a jump: flat-forward jumps by over 10bp
}

TEST_CASE("curve: rate conversions") {
    const InterpolatedCurve curve(kAsOf, {1.0, 30.0}, {0.05, 0.05},
                                  InterpolationMethod::FlatForward);
    const Date d1(2027, 11, 17);
    const Date d2(2028, 11, 16);
    CHECK(curve.discount(d1) == doctest::Approx(std::exp(-0.05)).epsilon(1e-14));
    CHECK(curve.zero_rate(d2) == doctest::Approx(0.05).epsilon(1e-13));
    CHECK(curve.zero_rate_compounded(d1, 2) ==
          doctest::Approx(2.0 * (std::exp(0.025) - 1.0)).epsilon(1e-13));
    CHECK(curve.forward_rate(d1, d2) == doctest::Approx(0.05).epsilon(1e-12));
    CHECK(curve.simple_forward_rate(d1, d2, DayCount::Act365F) ==
          doctest::Approx((std::exp(0.05 * 365.0 / 365.0) - 1.0)).epsilon(1e-12));
    CHECK_THROWS_AS(curve.discount(Date(2026, 11, 16)), Error);
}

TEST_CASE("curve: extrapolation names and each scheme's natural choice") {
    CHECK(parse_extrapolation("flat-forward") == Extrapolation::FlatForward);
    CHECK(parse_extrapolation("Flat-Zero") == Extrapolation::FlatZero);
    CHECK(to_string(Extrapolation::FlatZero) == "flat-zero");
    CHECK_THROWS_WITH(parse_extrapolation("sideways"),
                      "unknown extrapolation 'sideways' (expected flat-forward or flat-zero)");
    // ADR 0007: each scheme extrapolates in the quantity it interpolates.
    CHECK(natural_extrapolation(InterpolationMethod::LinearZero) == Extrapolation::FlatZero);
    CHECK(natural_extrapolation(InterpolationMethod::CubicZero) == Extrapolation::FlatZero);
    CHECK(natural_extrapolation(InterpolationMethod::FlatForward) == Extrapolation::FlatForward);
    CHECK(natural_extrapolation(InterpolationMethod::MonotoneCubic) == Extrapolation::FlatForward);
    CHECK(natural_extrapolation(InterpolationMethod::MonotoneConvex) == Extrapolation::FlatForward);
}

TEST_CASE("curve: extrapolation beyond the last pillar under every scheme") {
    const std::vector<double> times{1.0, 5.0, 10.0};
    const std::vector<double> zeros{0.03, 0.04, 0.045};
    for (const auto method : kAllMethods) {
        CAPTURE(to_string(method));
        const InterpolatedCurve natural(kAsOf, times, zeros, method);
        const InterpolatedCurve flat_forward(kAsOf, times, zeros, method,
                                             Extrapolation::FlatForward);
        const InterpolatedCurve flat_zero(kAsOf, times, zeros, method, Extrapolation::FlatZero);
        CHECK(natural.extrapolation() == natural_extrapolation(method));

        // Inside the pillar range the choice changes nothing.
        for (int k = 1; k <= 100; ++k) {
            const double t = 0.1 * k;
            CHECK(flat_forward.discount_t(t) ==
                  doctest::Approx(natural.discount_t(t)).epsilon(1e-15));
            CHECK(flat_zero.discount_t(t) == doctest::Approx(natural.discount_t(t)).epsilon(1e-15));
        }
        for (const double t : {10.5, 20.0, 50.0}) {
            CAPTURE(t);
            const InterpolatedCurve& same =
                natural_extrapolation(method) == Extrapolation::FlatZero ? flat_zero : flat_forward;
            CHECK(natural.discount_t(t) == doctest::Approx(same.discount_t(t)).epsilon(1e-15));
            // flat-zero: the zero rate stays at the last pillar's.
            CHECK(flat_zero.zero_rate_t(t) == doctest::Approx(0.045).epsilon(1e-14));
            // flat-forward: the forward stays at its value at the last pillar.
            CHECK(flat_forward.instantaneous_forward_t(t) ==
                  doctest::Approx(flat_forward.instantaneous_forward_t(10.5)).epsilon(1e-10));
        }
        // flat-forward continues the forward from inside the curve, so it does not jump at the
        // last pillar; flat-zero jumps from that forward to the zero rate, unless they coincide.
        CHECK(std::fabs(forward_jump(flat_forward, 10.0, 1e-5)) < 1e-6);
        CHECK(std::fabs(forward_jump(flat_zero, 10.0, 1e-5)) > 1e-3);
    }

    // Hand calculations. The last forward is z_n + t_n z'(t_n) on the zero-rate schemes, the
    // slope of -ln DF at t_n on the others; -ln DF(20) = 0.45 + 10 f_n.
    // linear-zero: z' = (4.5% - 4%) / 5 = 0.1%, so f_n = 5.5% and z(20) = (0.45 + 0.55) / 20.
    const InterpolatedCurve linear(kAsOf, times, zeros, InterpolationMethod::LinearZero,
                                   Extrapolation::FlatForward);
    CHECK(linear.zero_rate_t(20.0) == doctest::Approx(0.05).epsilon(1e-14));
    CHECK(linear.instantaneous_forward_t(20.0) == doctest::Approx(0.055).epsilon(1e-10));
    // cubic-zero: natural spline with second derivative -0.0005 at t = 5 (from
    // 18 m = 6 (0.1% - 0.25%)), end slope z' = 0.1% + 5 * (-0.0005) / 6, f_n = 4.5% + 10 z'.
    const InterpolatedCurve cubic(kAsOf, times, zeros, InterpolationMethod::CubicZero,
                                  Extrapolation::FlatForward);
    const double cubic_forward = 0.045 + 10.0 * (0.001 - 5.0 * 0.0005 / 6.0);
    CHECK(cubic.zero_rate_t(20.0) ==
          doctest::Approx((0.45 + 10.0 * cubic_forward) / 20.0).epsilon(1e-14));
    // flat-forward interpolation: the last discrete forward (0.45 - 0.20) / 5 = 5%.
    const InterpolatedCurve flat(kAsOf, times, zeros, InterpolationMethod::FlatForward);
    CHECK(flat.zero_rate_t(20.0) == doctest::Approx((0.45 + 0.5) / 20.0).epsilon(1e-14));

    // A single pillar: linear-zero has no slope, so both choices give the same flat curve.
    const InterpolatedCurve one(kAsOf, {2.0}, {0.04}, InterpolationMethod::LinearZero,
                                Extrapolation::FlatForward);
    CHECK(one.zero_rate_t(9.0) == doctest::Approx(0.04).epsilon(1e-14));
}

TEST_CASE("curve: invalid nodes are rejected") {
    CHECK_THROWS_AS(InterpolatedCurve(kAsOf, {}, {}, InterpolationMethod::LinearZero), Error);
    CHECK_THROWS_AS(
        InterpolatedCurve(kAsOf, {0.0, 1.0}, {0.01, 0.01}, InterpolationMethod::LinearZero), Error);
    CHECK_THROWS_AS(
        InterpolatedCurve(kAsOf, {2.0, 1.0}, {0.01, 0.01}, InterpolationMethod::FlatForward),
        Error);
}

TEST_CASE("zero shift: parallel, piecewise and key-rate shapes") {
    CHECK(ZeroShift().is_zero());
    CHECK(ZeroShift::parallel(0.001).at(12.3) == 0.001);

    const ZeroShift twist = ZeroShift::piecewise({2.0, 10.0}, {-0.0025, 0.0025});
    CHECK(twist.at(1.0) == doctest::Approx(-0.0025));
    CHECK(twist.at(6.0) == doctest::Approx(0.0));
    CHECK(twist.at(30.0) == doctest::Approx(0.0025));

    // Key-rate bumps partition a parallel shift.
    const std::vector<double> keys{2.0, 5.0, 10.0, 30.0};
    for (const double t : {0.5, 2.0, 3.7, 5.0, 8.1, 20.0, 45.0}) {
        double sum = 0.0;
        for (std::size_t k = 0; k < keys.size(); ++k) {
            sum += ZeroShift::key_rate(keys, k, 1e-4).at(t);
        }
        CHECK(sum == doctest::Approx(1e-4).epsilon(1e-13));
    }
    CHECK(ZeroShift::key_rate(keys, 1, 1e-4).at(3.5) == doctest::Approx(0.5e-4));
}

TEST_CASE("curve: shifted, forward-implied and rebased curves") {
    const auto base = std::make_shared<InterpolatedCurve>(
        kAsOf, std::vector<double>{1.0, 5.0, 10.0}, std::vector<double>{0.03, 0.04, 0.045},
        InterpolationMethod::LinearZero);

    const ShiftedCurve up(base, ZeroShift::parallel(0.01));
    CHECK(up.zero_rate_t(5.0) == doctest::Approx(0.05).epsilon(1e-13));
    CHECK(shifted(base, ZeroShift()) == base); // no wrapper for a zero shift

    const Date horizon(2027, 11, 17);
    const ForwardImpliedCurve forward(base, horizon);
    const Date later(2031, 11, 17);
    CHECK(forward.as_of() == horizon);
    CHECK(forward.discount(later) ==
          doctest::Approx(base->discount(later) / base->discount(horizon)).epsilon(1e-14));

    const RebasedCurve rolled(base, horizon);
    CHECK(rolled.as_of() == horizon);
    CHECK(rolled.zero_rate_t(5.0) == doctest::Approx(0.04).epsilon(1e-13));
}

TEST_CASE("bootstrap: every quote reprices under every interpolation method") {
    for (const auto method : kAllMethods) {
        CAPTURE(to_string(method));
        const BootstrapResult r = bootstrap(kAsOf, treasury_quotes(), Calendar(), {method});
        CHECK(r.max_error < 1e-12);
        REQUIRE(r.quotes.size() == 11);
        for (std::size_t i = 0; i < r.quotes.size(); ++i) {
            CHECK(implied_quote_rate(r.quotes[i], *r.curve, Calendar()) ==
                  doctest::Approx(r.quotes[i].rate).epsilon(1e-10));
        }

        const Calendar us = sofr_conventions().calendar;
        const BootstrapResult sofr = bootstrap(kAsOf, sofr_quotes(), us, {method});
        CHECK(sofr.max_error < 1e-12);
        REQUIRE(sofr.quotes.size() == 13);
        for (std::size_t i = 0; i < sofr.quotes.size(); ++i) {
            CHECK(implied_quote_rate(sofr.quotes[i], *sofr.curve, us) ==
                  doctest::Approx(sofr.quotes[i].rate).epsilon(1e-10));
        }
    }
}

TEST_CASE("bootstrap: negative rates under every interpolation method") {
    // Negative short rates turning positive at the long end; every scheme must still reprice.
    struct Row {
        QuoteType type;
        const char* tenor;
        double rate;
    };
    const Row rows[] = {{QuoteType::Deposit, "1M", -0.0055}, {QuoteType::Deposit, "6M", -0.0050},
                        {QuoteType::Swap, "1Y", -0.0048},    {QuoteType::Swap, "2Y", -0.0045},
                        {QuoteType::Swap, "5Y", -0.0028},    {QuoteType::Swap, "10Y", 0.0002},
                        {QuoteType::Swap, "20Y", 0.0015},    {QuoteType::Swap, "30Y", 0.0010}};
    std::vector<Quote> quotes;
    for (const Row& row : rows) {
        quotes.push_back(make_quote(kAsOf, row.type, row.tenor, row.rate, {}));
    }
    for (const auto method : kAllMethods) {
        CAPTURE(to_string(method));
        const BootstrapResult r = bootstrap(kAsOf, quotes, Calendar(), {method});
        CHECK(r.max_error < 1e-12);
        CHECK(r.curve->instantaneous_forward_t(0.5) < 0.0);
    }
}

TEST_CASE("bootstrap: monotone convex discount factors match QuantLib") {
    // Reference: QuantLib 1.44 PiecewiseConvexMonotoneForward with ConvexMonotone(0, 1, false)
    // (the Hagan-West method without QuantLib's blending parameters) built from the same
    // DepositRateHelper / FixedRateBondHelper set as tools/quantlib_crosscheck.py. The two
    // bootstraps agree to 1.4e-12 at every pillar and date tested inside the pillar range.
    const BootstrapResult r =
        bootstrap(kAsOf, treasury_quotes(), Calendar(), {InterpolationMethod::MonotoneConvex});
    CHECK(r.passes > 1); // not a purely local scheme: later pillars move earlier segments
    const double tol = 1e-10;
    CHECK(r.curve->discount(Date(2028, 11, 17)) == doctest::Approx(0.925726006008).epsilon(tol));
    CHECK(r.curve->discount(Date(2036, 11, 17)) == doctest::Approx(0.653968429864).epsilon(tol));
    CHECK(r.curve->discount(Date(2056, 11, 17)) == doctest::Approx(0.253063616820).epsilon(tol));
    CHECK(r.curve->discount(Date(2026, 12, 1)) == doctest::Approx(0.998320345834).epsilon(tol));
    CHECK(r.curve->discount(Date(2027, 8, 3)) == doctest::Approx(0.971289888186).epsilon(tol));
    CHECK(r.curve->discount(Date(2030, 1, 15)) == doctest::Approx(0.885805751248).epsilon(tol));
    CHECK(r.curve->discount(Date(2033, 5, 5)) == doctest::Approx(0.771233403037).epsilon(tol));
    CHECK(r.curve->discount(Date(2041, 6, 30)) == doctest::Approx(0.516061545237).epsilon(tol));
    CHECK(r.curve->discount(Date(2050, 2, 2)) == doctest::Approx(0.337368575979).epsilon(tol));
}

TEST_CASE("bootstrap: discount factors match QuantLib") {
    // Reference: QuantLib 1.x PiecewiseLogLinearDiscount / PiecewiseLinearZero built from
    // DepositRateHelper and FixedRateBondHelper (see tools/quantlib_crosscheck.py).
    const BootstrapResult flat =
        bootstrap(kAsOf, treasury_quotes(), Calendar(), {InterpolationMethod::FlatForward});
    CHECK(flat.passes == 1); // local scheme: a single sweep is exact
    CHECK(flat.curve->discount(Date(2026, 12, 17)) == doctest::Approx(0.9964129135).epsilon(1e-9));
    CHECK(flat.curve->discount(Date(2027, 11, 17)) == doctest::Approx(0.9607143048).epsilon(1e-9));
    CHECK(flat.curve->discount(Date(2031, 11, 17)) == doctest::Approx(0.8222574082).epsilon(1e-9));
    CHECK(flat.curve->discount(Date(2036, 11, 17)) == doctest::Approx(0.6540839453).epsilon(1e-9));
    CHECK(flat.curve->discount(Date(2056, 11, 17)) == doctest::Approx(0.2525759486).epsilon(1e-9));
    CHECK(flat.curve->discount(Date(2041, 6, 30)) == doctest::Approx(0.5157248219).epsilon(1e-9));

    const BootstrapResult linear =
        bootstrap(kAsOf, treasury_quotes(), Calendar(), {InterpolationMethod::LinearZero});
    CHECK(linear.curve->discount(Date(2036, 11, 17)) ==
          doctest::Approx(0.6539739307).epsilon(1e-9));
    CHECK(linear.curve->discount(Date(2046, 11, 19)) ==
          doctest::Approx(0.3892779025).epsilon(1e-9));
    CHECK(linear.curve->discount(Date(2050, 2, 2)) == doctest::Approx(0.3376531048).epsilon(1e-9));
}

TEST_CASE("bootstrap: extrapolation matches QuantLib beyond the last pillar") {
    // Reference: QuantLib 1.44 PiecewiseLogLinearDiscount and PiecewiseLinearZero, built from the
    // helpers in tools/quantlib_crosscheck.py with extrapolation enabled. Both hold the
    // instantaneous forward at the last pillar (InterpolatedZeroCurve uses z + t z'(t)); the
    // last pillar is 2056-11-17.
    const double tol = 1e-10;
    const BootstrapResult flat =
        bootstrap(kAsOf, treasury_quotes(), Calendar(), {InterpolationMethod::FlatForward});
    CHECK(flat.curve->extrapolation() == Extrapolation::FlatForward);
    CHECK(flat.curve->discount(Date(2060, 1, 1)) == doctest::Approx(0.220397818657).epsilon(tol));
    CHECK(flat.curve->discount(Date(2076, 11, 17)) == doctest::Approx(0.105473330396).epsilon(tol));

    BootstrapOptions options{InterpolationMethod::LinearZero};
    options.extrapolation = Extrapolation::FlatForward;
    const BootstrapResult linear = bootstrap(kAsOf, treasury_quotes(), Calendar(), options);
    CHECK(linear.curve->extrapolation() == Extrapolation::FlatForward);
    CHECK(linear.curve->discount(Date(2060, 1, 1)) == doctest::Approx(0.220658048373).epsilon(tol));
    CHECK(linear.curve->discount(Date(2076, 11, 17)) ==
          doctest::Approx(0.107936896773).epsilon(tol));
}

TEST_CASE("bootstrap: the extrapolation choice does not move the nodes") {
    // No bootstrap instrument pays beyond its own pillar, so the solved nodes are the same.
    for (const auto method : kAllMethods) {
        CAPTURE(to_string(method));
        const BootstrapResult natural = bootstrap(kAsOf, treasury_quotes(), Calendar(), {method});
        for (const auto extrapolation : {Extrapolation::FlatForward, Extrapolation::FlatZero}) {
            BootstrapOptions options{method};
            options.extrapolation = extrapolation;
            const BootstrapResult r = bootstrap(kAsOf, treasury_quotes(), Calendar(), options);
            CHECK(r.curve->extrapolation() == extrapolation);
            CHECK(r.passes == natural.passes);
            for (std::size_t i = 0; i < r.quotes.size(); ++i) {
                CHECK(r.curve->zero_rates()[i] ==
                      doctest::Approx(natural.curve->zero_rates()[i]).epsilon(1e-15));
            }
        }
    }
}

TEST_CASE("bootstrap: quote conventions") {
    QuoteConventions conventions;
    conventions.calendar = Calendar(Calendar::Rules::UsGovernmentBond);
    conventions.settle_days = 2;
    // T+2 from Tuesday 17 Nov 2026 is Thursday 19 Nov; one week later is Thanksgiving.
    const Quote deposit = make_quote(kAsOf, QuoteType::Deposit, "1W", 0.0433, conventions);
    CHECK(deposit.start == Date(2026, 11, 19));
    CHECK(deposit.pillar == Date(2026, 11, 27));
    CHECK(deposit.day_count == DayCount::Act360);

    const Quote swap = make_quote(kAsOf, QuoteType::Swap, "2Y", 0.0374, conventions);
    CHECK(swap.frequency == 1);
    CHECK(swap.pillar == Date(2028, 11, 20));

    const Quote dated = make_quote(kAsOf, QuoteType::Zero, "2030-06-30", 0.04, conventions);
    CHECK(dated.pillar == Date(2030, 6, 30));
    CHECK(dated.start == kAsOf);

    CHECK_THROWS_AS(make_quote(kAsOf, QuoteType::Par, "2026-11-01", 0.04, conventions), Error);
    CHECK_THROWS_AS(parse_quote_type("future"), Error);
}

TEST_CASE("bootstrap: swap periods accrue between adjusted dates") {
    // A 2Y annual ACT/360 swap from Thursday 2026-11-19 on a flat 4% curve. The maturity,
    // 2028-11-19, is a Sunday, so the last period runs to Monday 2028-11-20 (modified
    // following): accruals 365/360 and 367/360 days, the latter crossing 29 Feb 2028.
    const InterpolatedCurve curve(kAsOf, {1.0, 5.0}, {0.04, 0.04}, InterpolationMethod::LinearZero);
    const Date start(2026, 11, 19);
    const Date first(2027, 11, 19);
    const Date last(2028, 11, 20);
    const double annuity =
        365.0 / 360.0 * curve.discount(first) + 367.0 / 360.0 * curve.discount(last);
    const double expected = (curve.discount(start) - curve.discount(last)) / annuity;
    CHECK(swap_rate(curve, start, Date(2028, 11, 19), 1, DayCount::Act360,
                    Calendar(Calendar::Rules::WeekendsOnly)) ==
          doctest::Approx(expected).epsilon(1e-15));
}

TEST_CASE("bootstrap: SOFR OIS discount factors match QuantLib") {
    // Reference: QuantLib 1.44 PiecewiseLogLinearDiscount from DepositRateHelper (2 fixing days,
    // US government bond calendar, modified following, ACT/360) and OISRateHelper(2, tenor,
    // rate, overnight index on that calendar with ACT/360), whose defaults accrue each fixed
    // period between modified-following adjusted dates. See tools/quantlib_crosscheck.py. The
    // 2Y and 7Y pillars are where accrual dates fall on weekends.
    const Calendar us = sofr_conventions().calendar;
    const BootstrapResult r = bootstrap(kAsOf, sofr_quotes(), us);
    const double tol = 1e-10;
    CHECK(r.curve->discount(Date(2028, 11, 20)) == doctest::Approx(0.927933095786).epsilon(tol));
    CHECK(r.curve->discount(Date(2033, 11, 21)) == doctest::Approx(0.766560565045).epsilon(tol));
    CHECK(r.curve->discount(Date(2056, 11, 20)) == doctest::Approx(0.303079304576).epsilon(tol));
    CHECK(r.curve->discount(Date(2030, 1, 15)) == doctest::Approx(0.890537861461).epsilon(tol));
    CHECK(r.curve->discount(Date(2050, 2, 2)) == doctest::Approx(0.387922247178).epsilon(tol));
}

TEST_CASE("bootstrap: irs quote conventions") {
    const QuoteConventions conventions = sofr_conventions();
    const Quote irs = make_quote(kAsOf, QuoteType::Irs, "2Y", 0.0381, conventions);
    CHECK(parse_quote_type("IRS") == QuoteType::Irs);
    CHECK(irs.day_count == DayCount::Act360);
    CHECK(irs.frequency == 1);
    CHECK(irs.float_frequency == 4); // default: a 3M term rate paid quarterly
    CHECK(irs.pillar == Date(2028, 11, 20));
    CHECK(make_quote(kAsOf, QuoteType::Irs, "2Y", 0.0381, conventions, "30/360", 2, 2)
              .float_frequency == 2);
    CHECK_THROWS_WITH(make_quote(kAsOf, QuoteType::Swap, "2Y", 0.0374, conventions, "", 0, 4),
                      "quote '2Y': float_frequency applies to irs quotes only");
}

TEST_CASE("swap rate: floating legs on a projection curve") {
    // Flat curves: projection 5%, discount 4%, continuously compounded on ACT/365F from the
    // as-of date. A 1Y swap from Thursday 2026-11-19: annual ACT/360 fixed to 2027-11-19 (365
    // days) against quarterly floating periods ending 2027-02-19, 05-19, 08-19 and 11-19, all
    // business days. Each floating coupon is worth DF(e) (P(s) / P(e) - 1).
    const InterpolatedCurve projection(kAsOf, {1.0}, {0.05}, InterpolationMethod::FlatForward);
    const InterpolatedCurve discount(kAsOf, {1.0}, {0.04}, InterpolationMethod::FlatForward);
    const Calendar weekends(Calendar::Rules::WeekendsOnly);
    const std::vector<Date> dates{Date(2026, 11, 19), Date(2027, 2, 19), Date(2027, 5, 19),
                                  Date(2027, 8, 19), Date(2027, 11, 19)};
    const auto t = [](Date d) { return (d - kAsOf) / 365.0; };
    double floating = 0.0;
    for (std::size_t j = 1; j < dates.size(); ++j) {
        floating += std::exp(-0.04 * t(dates[j])) *
                    (std::exp(0.05 * (t(dates[j]) - t(dates[j - 1]))) - 1.0);
    }
    const double annuity = 365.0 / 360.0 * std::exp(-0.04 * t(dates.back()));
    CHECK(swap_rate(projection, discount, dates.front(), dates.back(), 1, DayCount::Act360, 4,
                    weekends) == doctest::Approx(floating / annuity).epsilon(1e-15));
    // On one curve the floating frequency drops out: the legs telescope.
    CHECK(swap_rate(discount, discount, dates.front(), dates.back(), 1, DayCount::Act360, 4,
                    weekends) == doctest::Approx(swap_rate(discount, dates.front(), dates.back(), 1,
                                                           DayCount::Act360, weekends))
                                     .epsilon(1e-15));
}

TEST_CASE("bootstrap: a projection curve reprices on its discount curve") {
    for (const auto method : kAllMethods) {
        CAPTURE(to_string(method));
        CurvePtr discount;
        const BootstrapResult r = term_sofr_curve(method, &discount);
        CHECK(r.max_error < 1e-12);
        REQUIRE(r.quotes.size() == 10);
        for (const Quote& q : r.quotes) {
            CHECK(implied_quote_rate(q, *r.curve, *discount, sofr_conventions().calendar) ==
                  doctest::Approx(q.rate).epsilon(1e-10));
        }
        // The projection forwards sit above the OIS ones: the 3M basis.
        CHECK(r.curve->zero_rate(Date(2036, 11, 19)) > discount->zero_rate(Date(2036, 11, 19)));
    }
}

TEST_CASE("bootstrap: projection discount factors match QuantLib dual-curve") {
    // Reference: QuantLib 1.44. The discount curve is built as in "SOFR OIS discount factors
    // match QuantLib". The projection curve is PiecewiseLogLinearDiscount / PiecewiseLinearZero
    // from DepositRateHelper(3M, 2 fixing days) and SwapRateHelper(annual, modified following,
    // ACT/360, IborIndex 3M on the US government bond calendar, discountingCurve = the OIS curve,
    // withIndexedCoupons = false, i.e. par coupons). The agreement is 2e-12.
    const double tol = 1e-10;
    const BootstrapResult flat = term_sofr_curve(InterpolationMethod::FlatForward);
    CHECK(flat.curve->discount(Date(2028, 11, 20)) == doctest::Approx(0.926637370138).epsilon(tol));
    CHECK(flat.curve->discount(Date(2036, 11, 19)) == doctest::Approx(0.669137638033).epsilon(tol));
    CHECK(flat.curve->discount(Date(2056, 11, 20)) == doctest::Approx(0.295960210600).epsilon(tol));
    CHECK(flat.curve->discount(Date(2030, 1, 15)) == doctest::Approx(0.888294894312).epsilon(tol));
    CHECK(flat.curve->discount(Date(2050, 2, 2)) == doctest::Approx(0.380858744214).epsilon(tol));
    const BootstrapResult linear = term_sofr_curve(InterpolationMethod::LinearZero);
    CHECK(linear.curve->discount(Date(2046, 11, 19)) ==
          doctest::Approx(0.428763238618).epsilon(tol));
    CHECK(linear.curve->discount(Date(2050, 2, 2)) == doctest::Approx(0.379349331779).epsilon(tol));
}

TEST_CASE("bootstrap: on one curve irs and swap quotes agree") {
    // Without a separate discount curve the floating leg telescopes whatever its frequency, so an
    // irs is the OIS swap with the same fixed leg.
    const BootstrapResult sofr = bootstrap(kAsOf, sofr_quotes(), sofr_conventions().calendar);
    const Quote irs = make_quote(kAsOf, QuoteType::Irs, "7Y", 0.038, sofr_conventions());
    const Quote swap = make_quote(kAsOf, QuoteType::Swap, "7Y", 0.038, sofr_conventions());
    CHECK(implied_quote_rate(irs, *sofr.curve, sofr_conventions().calendar) ==
          doctest::Approx(implied_quote_rate(swap, *sofr.curve, sofr_conventions().calendar))
              .epsilon(1e-15));
}

TEST_CASE("bootstrap: projection curve inputs are checked") {
    const Calendar us = sofr_conventions().calendar;
    BootstrapOptions options;
    options.discount = bootstrap(kAsOf, sofr_quotes(), us).curve;
    // A par bond quote cannot sit on a curve discounted elsewhere.
    const Quote par = make_quote(kAsOf, QuoteType::Par, "5Y", 0.04, sofr_conventions());
    CHECK_THROWS_WITH(bootstrap(kAsOf, {par}, us, options),
                      "quote '5Y': a par bond quote prices on the curve itself, so it cannot be "
                      "used on a curve discounted on another curve");
    // The discount curve must be as of the same date.
    options.discount = std::make_shared<InterpolatedCurve>(
        Date(2026, 11, 18), std::vector{1.0}, std::vector{0.04}, InterpolationMethod::FlatForward);
    CHECK_THROWS_WITH(bootstrap(kAsOf, term_sofr_quotes(), us, options),
                      "bootstrap: the discount curve is as of 2026-11-18, not 2026-11-17");
}

TEST_CASE("bootstrap: a single swap reprices and matches the closed form") {
    // One annual-pay 1Y swap: r = (1 - DF) / (tau * DF)  =>  DF = 1 / (1 + r * tau).
    const Quote q = make_quote(kAsOf, QuoteType::Swap, "1Y", 0.04, {});
    const BootstrapResult r = bootstrap(kAsOf, {q}, Calendar());
    CHECK(r.curve->discount(Date(2027, 11, 17)) ==
          doctest::Approx(1.0 / (1.0 + 0.04 * 365.0 / 360.0)).epsilon(1e-13));
}

TEST_CASE("bootstrap: bad inputs are reported with the offending quote") {
    const Quote a = make_quote(kAsOf, QuoteType::Zero, "1Y", 0.04, {});
    const Quote b = make_quote(kAsOf, QuoteType::Zero, "12M", 0.041, {});
    CHECK_THROWS_WITH(bootstrap(kAsOf, {a, b}, Calendar()),
                      "quotes '1Y' and '12M' mature on the same date 2027-11-17");
    CHECK_THROWS_AS(bootstrap(kAsOf, {}, Calendar()), Error);
}

TEST_CASE("par rate: a bootstrapped par curve returns its inputs") {
    const BootstrapResult r = bootstrap(kAsOf, treasury_quotes(), Calendar());
    CHECK(par_rate(*r.curve, kAsOf, Date(2036, 11, 17), 2, DayCount::ActActICMA, Calendar()) ==
          doctest::Approx(0.0425).epsilon(1e-10));
}

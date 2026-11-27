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
    CHECK_THROWS_AS(parse_quote_type("cap"), Error);
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
                      "quote '2Y': float_frequency applies to irs and basis quotes only");
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

std::vector<Quote> sofr_futures_quotes() {
    return load_quotes(std::string(FIT_EXAMPLES_DIR) + "/usd_sofr_futures.csv", kAsOf,
                       sofr_conventions());
}

TEST_CASE("bootstrap: FRA quote conventions") {
    // As QuantLib's FraRateHelper: start A months after spot (T+2: 2026-11-19), end B - A months
    // after the start, both modified following.
    const QuoteConventions conventions = sofr_conventions();
    const Quote fra = make_quote(kAsOf, QuoteType::Fra, "3x6", 0.0397, conventions);
    CHECK(parse_quote_type("FRA") == QuoteType::Fra);
    CHECK(fra.start == Date(2027, 2, 19));
    CHECK(fra.pillar == Date(2027, 5, 19));
    CHECK(fra.day_count == DayCount::Act360);
    CHECK(make_quote(kAsOf, QuoteType::Fra, "0x3", 0.04, conventions).start == Date(2026, 11, 19));
    CHECK(make_quote(kAsOf, QuoteType::Fra, "9X12", 0.04, conventions).pillar ==
          Date(2027, 11, 19));
    for (const char* bad : {"6x3", "3x3", "3-6", "x6", "3x", "3x6M"}) {
        CAPTURE(bad);
        CHECK_THROWS_AS(make_quote(kAsOf, QuoteType::Fra, bad, 0.04, conventions), Error);
    }
}

TEST_CASE("bootstrap: futures quote conventions") {
    // A quarterly IMM contract's period runs from the third Wednesday of its month to the third
    // Wednesday three months later (QuantLib's SofrFutureRateHelper gives the same dates).
    const QuoteConventions conventions = sofr_conventions();
    const Quote z6 =
        make_quote(kAsOf, QuoteType::Future, "Z6", 0.0421, conventions, "", 0, 0, 1e-5);
    CHECK(parse_quote_type("futures") == QuoteType::Future);
    CHECK(z6.start == Date(2026, 12, 16));
    CHECK(z6.pillar == Date(2027, 3, 17));
    CHECK(z6.convexity == 1e-5);
    // A one-digit year is the first year from the as-of year on that ends in it.
    CHECK(make_quote(kAsOf, QuoteType::Future, "H7", 0.04, conventions).start == Date(2027, 3, 17));
    CHECK(make_quote(kAsOf, QuoteType::Future, "u27", 0.04, conventions).pillar ==
          Date(2027, 12, 15));
    // ...across a decade: in November 2029, H0 is March 2030 (1 Mar 2030 is a Friday).
    CHECK(make_quote(Date(2029, 11, 1), QuoteType::Future, "H0", 0.04, conventions).start ==
          Date(2030, 3, 20));
    // March 2026 has passed: the contract is in or past its period, so it is rejected rather
    // than read as 2036.
    CHECK_THROWS_WITH(make_quote(kAsOf, QuoteType::Future, "H6", 0.04, conventions),
                      "futures contract 'H6': its period starts 2026-03-18, before the as-of "
                      "date; a contract already in its period needs past fixings, which fit "
                      "does not take");
    for (const char* bad : {"Q7", "Z", "Z123", "ZX", "2026-12-16"}) {
        CAPTURE(bad);
        CHECK_THROWS_AS(make_quote(kAsOf, QuoteType::Future, bad, 0.04, conventions), Error);
    }
    CHECK_THROWS_WITH(
        make_quote(kAsOf, QuoteType::Deposit, "3M", 0.04, conventions, "", 0, 0, 1e-5),
        "quote '3M': a convexity adjustment applies to futures only");
}

TEST_CASE("bootstrap: FRA and futures rates on a curve") {
    // On a flat 4% continuously compounded ACT/365F curve, the simple ACT/360 forward over
    // [s, e] is (exp(0.04 (e - s) / 365) - 1) * 360 / (e - s). A future adds its convexity
    // adjustment: futures rate = forward + adjustment.
    const InterpolatedCurve curve(kAsOf, {1.0}, {0.04}, InterpolationMethod::FlatForward);
    const auto forward = [](int days) {
        return (std::exp(0.04 * days / 365.0) - 1.0) * 360.0 / days;
    };
    const Quote fra = make_quote(kAsOf, QuoteType::Fra, "3x6", 0.0, sofr_conventions());
    CHECK(implied_quote_rate(fra, curve, Calendar()) ==
          doctest::Approx(forward(Date(2027, 5, 19) - Date(2027, 2, 19))).epsilon(1e-14));
    const Quote z6 =
        make_quote(kAsOf, QuoteType::Future, "Z6", 0.0, sofr_conventions(), "", 0, 0, 2e-5);
    CHECK(implied_quote_rate(z6, curve, Calendar()) ==
          doctest::Approx(forward(Date(2027, 3, 17) - Date(2026, 12, 16)) + 2e-5).epsilon(1e-14));
}

TEST_CASE("bootstrap: futures are quoted by price, other quotes by rate") {
    const std::vector<Quote> quotes = sofr_futures_quotes();
    REQUIRE(quotes.size() == 15);
    // future,Z6,,95.79,0.1: rate (100 - 95.79) / 100, convexity 0.1bp.
    CHECK(quotes[2].type == QuoteType::Future);
    CHECK(quotes[2].rate == doctest::Approx(0.0421).epsilon(1e-15));
    CHECK(quotes[2].convexity == doctest::Approx(1e-5).epsilon(1e-15));
    CHECK(quotes[0].convexity == 0.0);
}

TEST_CASE("bootstrap: a futures strip and FRAs reprice and match QuantLib") {
    // Reference: QuantLib 1.44 PiecewiseLogLinearDiscount. The futures curve uses
    // DepositRateHelper, SofrFutureRateHelper(price, month, year, Quarterly, convexity) and
    // OISRateHelper; the FRA curve uses DepositRateHelper, FraRateHelper(rate, A, B, 2, US
    // government bond calendar, modified following, no EOM, ACT/360) and SwapRateHelper on the
    // SOFR OIS curve, as in "projection discount factors match QuantLib dual-curve". All
    // pillars and four off-pillar dates agree to 1.7e-12.
    const Calendar us = sofr_conventions().calendar;
    const double tol = 1e-10;
    const BootstrapResult futures = bootstrap(kAsOf, sofr_futures_quotes(), us);
    CHECK(futures.max_error < 1e-12);
    CHECK(futures.curve->discount(Date(2027, 3, 17)) ==
          doctest::Approx(0.986047066459).epsilon(tol));
    CHECK(futures.curve->discount(Date(2028, 3, 15)) ==
          doctest::Approx(0.949250985011).epsilon(tol));
    CHECK(futures.curve->discount(Date(2028, 11, 20)) ==
          doctest::Approx(0.927955028424).epsilon(tol));
    CHECK(futures.curve->discount(Date(2027, 8, 3)) ==
          doctest::Approx(0.971330467456).epsilon(tol));
    CHECK(futures.curve->discount(Date(2028, 1, 10)) ==
          doctest::Approx(0.955508856169).epsilon(tol));
    // The convexity adjustment lowers the forward: over Z7's period the curve's forward is the
    // futures rate 3.67% less 1.4bp.
    const double z7 =
        (futures.curve->discount(Date(2027, 12, 15)) / futures.curve->discount(Date(2028, 3, 15)) -
         1.0) *
        360.0 / (Date(2028, 3, 15) - Date(2027, 12, 15));
    CHECK(z7 == doctest::Approx(0.0367 - 0.00014).epsilon(1e-13));

    BootstrapOptions options;
    options.discount = bootstrap(kAsOf, sofr_quotes(), us).curve;
    const BootstrapResult fras =
        bootstrap(kAsOf,
                  load_quotes(std::string(FIT_EXAMPLES_DIR) + "/usd_term_sofr_3m_fra.csv", kAsOf,
                              sofr_conventions()),
                  us, options);
    CHECK(fras.max_error < 1e-12);
    CHECK(fras.curve->discount(Date(2027, 5, 19)) == doctest::Approx(0.979309469461).epsilon(tol));
    CHECK(fras.curve->discount(Date(2027, 11, 19)) == doctest::Approx(0.960854437985).epsilon(tol));
    CHECK(fras.curve->discount(Date(2027, 10, 1)) == doctest::Approx(0.965682764085).epsilon(tol));
}

std::vector<Quote> example_quotes(const std::string& file) {
    return load_quotes(std::string(FIT_EXAMPLES_DIR) + "/" + file, kAsOf, sofr_conventions());
}

// The example basis curves: 3M Term SOFR from SOFR / 3M basis swaps on the SOFR curve (whose
// index is the default reference), and 6M Term SOFR from 3M / 6M basis swaps against the
// irs-built 3M curve.
struct BasisCurves {
    CurvePtr sofr;
    CurvePtr term_3m; // from irs quotes
    BootstrapResult basis_3m;
    BootstrapResult basis_6m;
};

BasisCurves basis_curves(InterpolationMethod method) {
    const Calendar us = sofr_conventions().calendar;
    BasisCurves c;
    c.term_3m = term_sofr_curve(method, &c.sofr).curve;
    BootstrapOptions options{method};
    options.discount = c.sofr;
    c.basis_3m = bootstrap(kAsOf, example_quotes("usd_term_sofr_3m_basis.csv"), us, options);
    options.reference = c.term_3m;
    c.basis_6m = bootstrap(kAsOf, example_quotes("usd_term_sofr_6m.csv"), us, options);
    return c;
}

TEST_CASE("bootstrap: basis quote conventions") {
    const QuoteConventions conventions = sofr_conventions();
    const Quote basis = make_quote(kAsOf, QuoteType::Basis, "2Y", 0.0005, conventions);
    CHECK(parse_quote_type("Basis") == QuoteType::Basis);
    CHECK(to_string(QuoteType::Basis) == "basis");
    CHECK(basis.start == Date(2026, 11, 19));  // spot, T+2
    CHECK(basis.pillar == Date(2028, 11, 20)); // 2028-11-19 is a Sunday
    CHECK(basis.day_count == DayCount::Act360);
    CHECK(basis.frequency == 4);       // the reference leg: quarterly
    CHECK(basis.float_frequency == 4); // this curve's leg: quarterly
    const Quote six = make_quote(kAsOf, QuoteType::Basis, "2Y", 0.0005, conventions, "", 0, 2);
    CHECK(six.frequency == 4);
    CHECK(six.float_frequency == 2);
}

TEST_CASE("basis spread: floating legs on flat curves") {
    // Flat curves, continuously compounded on ACT/365F from the as-of date: projection 5%,
    // reference 4.5%, discount 4%. A 1Y basis swap from Thursday 2026-11-19 on business days
    // only: the reference leg pays quarterly to 2027-02-19, 05-19, 08-19 and 11-19 with the
    // spread accruing ACT/360; the projection leg pays semi-annually to 2027-05-19 and 11-19.
    // Each coupon is worth DF(e) (P(s) / P(e) - 1), and the spread makes the legs equal.
    const InterpolatedCurve projection(kAsOf, {1.0}, {0.05}, InterpolationMethod::FlatForward);
    const InterpolatedCurve reference(kAsOf, {1.0}, {0.045}, InterpolationMethod::FlatForward);
    const InterpolatedCurve discount(kAsOf, {1.0}, {0.04}, InterpolationMethod::FlatForward);
    const Calendar weekends(Calendar::Rules::WeekendsOnly);
    const auto t = [](Date d) { return (d - kAsOf) / 365.0; };
    const auto leg = [&](double rate, const std::vector<Date>& dates) {
        double value = 0.0;
        for (std::size_t j = 1; j < dates.size(); ++j) {
            value += std::exp(-0.04 * t(dates[j])) *
                     (std::exp(rate * (t(dates[j]) - t(dates[j - 1]))) - 1.0);
        }
        return value;
    };
    const std::vector<Date> quarterly{Date(2026, 11, 19), Date(2027, 2, 19), Date(2027, 5, 19),
                                      Date(2027, 8, 19), Date(2027, 11, 19)};
    const std::vector<Date> semiannual{Date(2026, 11, 19), Date(2027, 5, 19), Date(2027, 11, 19)};
    double annuity = 0.0;
    for (std::size_t j = 1; j < quarterly.size(); ++j) {
        annuity += (quarterly[j] - quarterly[j - 1]) / 360.0 * std::exp(-0.04 * t(quarterly[j]));
    }
    const double expected = (leg(0.05, semiannual) - leg(0.045, quarterly)) / annuity;
    CHECK(expected > 0.0); // the projection index fixes above the reference
    CHECK(basis_spread(projection, reference, discount, quarterly.front(), quarterly.back(), 4, 2,
                       DayCount::Act360, weekends) == doctest::Approx(expected).epsilon(1e-15));
    // The same index on both legs, paid at the same frequency, is worth no spread.
    CHECK(basis_spread(reference, reference, discount, quarterly.front(), quarterly.back(), 4, 4,
                       DayCount::Act360, weekends) == 0.0);
    // A basis swap is two swaps with the same fixed leg, one on each index.
    const auto swap = [&](const YieldCurve& index, int float_frequency) {
        return swap_rate(index, discount, quarterly.front(), quarterly.back(), 4, DayCount::Act360,
                         float_frequency, weekends);
    };
    CHECK(basis_spread(projection, reference, discount, quarterly.front(), quarterly.back(), 4, 2,
                       DayCount::Act360, weekends) ==
          doctest::Approx(swap(projection, 2) - swap(reference, 4)).epsilon(1e-14));
}

TEST_CASE("bootstrap: basis curves reprice under every interpolation method") {
    for (const auto method : kAllMethods) {
        CAPTURE(to_string(method));
        const BasisCurves c = basis_curves(method);
        const Calendar us = sofr_conventions().calendar;
        CHECK(c.basis_3m.max_error < 1e-12);
        CHECK(c.basis_6m.max_error < 1e-12);
        for (const Quote& q : c.basis_6m.quotes) {
            CHECK(implied_quote_rate(q, *c.basis_6m.curve, *c.sofr, us, c.term_3m.get()) ==
                  doctest::Approx(q.rate).epsilon(1e-10));
        }
        // The basis-built 3M curve is close to the irs-built one, whose implied basis the
        // example's spreads round to 0.1bp: within 0.5bp of zero rate at 10 years.
        CHECK(std::fabs(c.basis_3m.curve->zero_rate(Date(2036, 11, 19)) -
                        c.term_3m->zero_rate(Date(2036, 11, 19))) < 0.5e-4);
    }
}

TEST_CASE("bootstrap: basis curves match QuantLib") {
    // Reference: QuantLib 1.44, PiecewiseLogLinearDiscount (flat-forward) and PiecewiseLinearZero
    // (linear-zero), the SOFR and irs-built 3M curves as in "projection discount factors match
    // QuantLib dual-curve". The 3M curve: DepositRateHelper(3M) and
    // OvernightIborBasisSwapRateHelper(spread, tenor, 2, US government bond calendar, modified
    // following, no EOM, SOFR forecast on the OIS curve, 3M index, discount = OIS). The 6M curve:
    // DepositRateHelper(6M) and IborIborBasisSwapRateHelper(spread, tenor, 2, same calendar,
    // modified following, no EOM, base = 3M index on the irs-built curve, other = 6M index,
    // discount = OIS, bootstrapBaseCurve = false). Both pay the spread on the base leg, each leg
    // at its index's frequency. Pillars and off-pillar dates agree to 1.9e-12.
    const double tol = 1e-10;
    const BasisCurves flat = basis_curves(InterpolationMethod::FlatForward);
    const auto& f3 = *flat.basis_3m.curve;
    CHECK(f3.discount(Date(2027, 11, 19)) == doctest::Approx(0.960596843807).epsilon(tol));
    CHECK(f3.discount(Date(2036, 11, 19)) == doctest::Approx(0.669125944294).epsilon(tol));
    CHECK(f3.discount(Date(2056, 11, 20)) == doctest::Approx(0.295943410650).epsilon(tol));
    CHECK(f3.discount(Date(2030, 1, 15)) == doctest::Approx(0.888292419749).epsilon(tol));
    const auto& f6 = *flat.basis_6m.curve;
    CHECK(f6.discount(Date(2027, 5, 19)) == doctest::Approx(0.979386457807).epsilon(tol));
    CHECK(f6.discount(Date(2027, 11, 19)) == doctest::Approx(0.960163909537).epsilon(tol));
    CHECK(f6.discount(Date(2036, 11, 19)) == doctest::Approx(0.665479891808).epsilon(tol));
    CHECK(f6.discount(Date(2056, 11, 20)) == doctest::Approx(0.291537980275).epsilon(tol));
    CHECK(f6.discount(Date(2027, 8, 3)) == doctest::Approx(0.971400494626).epsilon(tol));
    CHECK(f6.discount(Date(2041, 6, 30)) == doctest::Approx(0.538502410451).epsilon(tol));
    const BasisCurves linear = basis_curves(InterpolationMethod::LinearZero);
    CHECK(linear.basis_3m.curve->discount(Date(2050, 2, 2)) ==
          doctest::Approx(0.379329513728).epsilon(tol));
    CHECK(linear.basis_6m.curve->discount(Date(2031, 11, 19)) ==
          doctest::Approx(0.825697913217).epsilon(tol));
    CHECK(linear.basis_6m.curve->discount(Date(2050, 2, 2)) ==
          doctest::Approx(0.374778428634).epsilon(tol));
}

TEST_CASE("bootstrap: the reference curve of basis quotes") {
    const Calendar us = sofr_conventions().calendar;
    const std::vector<Quote> quotes = example_quotes("usd_term_sofr_3m_basis.csv");
    const BasisCurves c = basis_curves(InterpolationMethod::FlatForward);
    // Naming the discount curve as the reference is the default.
    BootstrapOptions options;
    options.discount = c.sofr;
    options.reference = c.sofr;
    const BootstrapResult explicit_reference = bootstrap(kAsOf, quotes, us, options);
    for (const Date d : {Date(2028, 11, 20), Date(2041, 6, 30)}) {
        CHECK(explicit_reference.curve->discount(d) == c.basis_3m.curve->discount(d));
    }
    // A curve that discounts itself can still price basis swaps against a reference curve.
    BootstrapOptions self;
    self.reference = c.sofr;
    const BootstrapResult own = bootstrap(kAsOf, quotes, us, self);
    CHECK(own.max_error < 1e-12);
    for (const Quote& q : own.quotes) {
        CHECK(implied_quote_rate(q, *own.curve, *own.curve, us, c.sofr.get()) ==
              doctest::Approx(q.rate).epsilon(1e-10));
    }
    // Without a discount or a reference curve, a basis quote has nothing to price against.
    const std::string none = "quote '1Y': a basis swap prices against a reference curve, and this "
                             "curve has none: build it on a discount curve, the default reference, "
                             "or name a reference curve in a curve set";
    CHECK_THROWS_WITH(bootstrap(kAsOf, quotes, us), none.c_str());
    CHECK_THROWS_WITH(implied_quote_rate(quotes[1], *c.sofr, us), none.c_str());
    // The reference curve must be as of the same date.
    self.reference = std::make_shared<InterpolatedCurve>(
        Date(2026, 11, 18), std::vector{1.0}, std::vector{0.04}, InterpolationMethod::FlatForward);
    CHECK_THROWS_WITH(bootstrap(kAsOf, quotes, us, self),
                      "bootstrap: the reference curve is as of 2026-11-18, not 2026-11-17");
}

TEST_CASE("bootstrap: basis swaps are quoted by spread") {
    const std::vector<Quote> quotes = example_quotes("usd_term_sofr_6m.csv");
    REQUIRE(quotes.size() == 10);
    // basis,1Y,,4.6,2: 4.6bp on the quarterly reference leg, against a semi-annual leg.
    CHECK(quotes[1].type == QuoteType::Basis);
    CHECK(quotes[1].rate == doctest::Approx(4.6e-4).epsilon(1e-15));
    CHECK(quotes[1].frequency == 4);
    CHECK(quotes[1].float_frequency == 2);
    CHECK(quotes[0].type == QuoteType::Deposit);
    CHECK(quotes[0].rate == doctest::Approx(0.0414).epsilon(1e-15));
}

QuoteConventions lagged_conventions() {
    QuoteConventions conventions = sofr_conventions();
    conventions.payment_lag = 2;
    return conventions;
}

TEST_CASE("bootstrap: payment lag conventions") {
    // Swap legs pay two business days after each period's adjusted end, and the pillar moves to
    // the last payment: the 1Y SOFR swap from 2026-11-19 ends on Friday 2027-11-19 and pays on
    // Tuesday 2027-11-23. QuantLib's OISRateHelper(paymentLag = 2) gives the same pillar.
    const QuoteConventions lagged = lagged_conventions();
    for (const QuoteType type : {QuoteType::Swap, QuoteType::Irs, QuoteType::Basis}) {
        CAPTURE(to_string(type));
        const Quote q = make_quote(kAsOf, type, "1Y", 0.04, lagged);
        CHECK(q.payment_lag == 2);
        CHECK(q.maturity == Date(2027, 11, 19));
        CHECK(q.pillar == Date(2027, 11, 23));
    }
    // 2028-11-20 is a Monday; Thanksgiving falls on the 23rd, after the Wednesday payment.
    CHECK(make_quote(kAsOf, QuoteType::Swap, "2Y", 0.04, lagged).pillar == Date(2028, 11, 22));
    // Other quotes pay once, at their own dates, and ignore the lag.
    for (const QuoteType type : {QuoteType::Deposit, QuoteType::Fra, QuoteType::Par}) {
        CAPTURE(to_string(type));
        const std::string tenor = type == QuoteType::Fra ? "3x6" : "1Y";
        const Quote q = make_quote(kAsOf, type, tenor, 0.04, lagged);
        CHECK(q.payment_lag == 0);
        CHECK(q.pillar == make_quote(kAsOf, type, tenor, 0.04, sofr_conventions()).pillar);
    }
    QuoteConventions negative = sofr_conventions();
    negative.payment_lag = -1;
    CHECK_THROWS_WITH(make_quote(kAsOf, QuoteType::Swap, "1Y", 0.04, negative),
                      "quote '1Y': the payment lag must be non-negative");
}

TEST_CASE("swap rate: legs paid after a lag on flat curves") {
    // A 2Y annual swap from Thursday 2026-11-19 on a flat 4% curve (continuously compounded,
    // ACT/365F), business days only. Periods end 2027-11-19 (Friday) and 2028-11-20 (Monday) and
    // pay two business days later, on 2027-11-23 and 2028-11-22. On one curve the floating coupons
    // no longer telescope: each is DF(pay) (DF(s) / DF(e) - 1).
    const InterpolatedCurve curve(kAsOf, {1.0}, {0.04}, InterpolationMethod::FlatForward);
    const Calendar weekends(Calendar::Rules::WeekendsOnly);
    const auto df = [](Date d) { return std::exp(-0.04 * (d - kAsOf) / 365.0); };
    const Date s(2026, 11, 19);
    const Date e1(2027, 11, 19);
    const Date e2(2028, 11, 20);
    const Date p1(2027, 11, 23);
    const Date p2(2028, 11, 22);
    const double floating = df(p1) * (df(s) / df(e1) - 1.0) + df(p2) * (df(e1) / df(e2) - 1.0);
    const double annuity = (e1 - s) / 360.0 * df(p1) + (e2 - e1) / 360.0 * df(p2);
    const double lagged = swap_rate(curve, s, Date(2028, 11, 19), 1, DayCount::Act360, weekends,
                                    BusinessDayConvention::ModifiedFollowing, 2);
    CHECK(lagged == doctest::Approx(floating / annuity).epsilon(1e-15));
    CHECK(lagged != swap_rate(curve, s, Date(2028, 11, 19), 1, DayCount::Act360, weekends));
    // With a projection curve, and for a basis swap, the lag moves every payment the same way: a
    // basis swap is still the difference of two lagged swaps with the same fixed leg.
    const InterpolatedCurve projection(kAsOf, {1.0}, {0.05}, InterpolationMethod::FlatForward);
    const auto swap = [&](const YieldCurve& index, int float_frequency) {
        return swap_rate(index, curve, s, Date(2028, 11, 19), 4, DayCount::Act360, float_frequency,
                         weekends, BusinessDayConvention::ModifiedFollowing, 2);
    };
    CHECK(basis_spread(projection, curve, curve, s, Date(2028, 11, 19), 4, 2, DayCount::Act360,
                       weekends, BusinessDayConvention::ModifiedFollowing,
                       2) == doctest::Approx(swap(projection, 2) - swap(curve, 4)).epsilon(1e-14));
}

TEST_CASE("bootstrap: lagged curves reprice and match QuantLib") {
    // Reference: QuantLib 1.44 PiecewiseLogLinearDiscount (flat-forward), the SOFR curve from
    // DepositRateHelper and OISRateHelper(2, tenor, rate, SOFR, no discount curve, no telescopic
    // value dates, paymentLag = 2), and the 3M curve on it from DepositRateHelper and
    // OvernightIborBasisSwapRateHelper(..., paymentLag = 2) as in "basis curves match QuantLib".
    // Pillars and off-pillar dates agree to 4.7e-13.
    const Calendar us = sofr_conventions().calendar;
    const double tol = 1e-10;
    for (const auto method : kAllMethods) {
        CAPTURE(to_string(method));
        const BootstrapResult r =
            bootstrap(kAsOf,
                      load_quotes(std::string(FIT_EXAMPLES_DIR) + "/usd_sofr.csv", kAsOf,
                                  lagged_conventions()),
                      us, {method});
        CHECK(r.max_error < 1e-12);
    }
    const BootstrapResult sofr = bootstrap(
        kAsOf,
        load_quotes(std::string(FIT_EXAMPLES_DIR) + "/usd_sofr.csv", kAsOf, lagged_conventions()),
        us);
    CHECK(sofr.quotes.back().pillar == Date(2056, 11, 22));
    CHECK(sofr.curve->discount(Date(2027, 11, 23)) == doctest::Approx(0.960774624459).epsilon(tol));
    CHECK(sofr.curve->discount(Date(2028, 11, 22)) == doctest::Approx(0.927754842753).epsilon(tol));
    CHECK(sofr.curve->discount(Date(2036, 11, 21)) == doctest::Approx(0.674310956363).epsilon(tol));
    CHECK(sofr.curve->discount(Date(2056, 11, 22)) == doctest::Approx(0.303019934541).epsilon(tol));
    CHECK(sofr.curve->discount(Date(2027, 8, 3)) == doctest::Approx(0.971869122130).epsilon(tol));
    CHECK(sofr.curve->discount(Date(2041, 6, 30)) == doctest::Approx(0.549140446097).epsilon(tol));
    BootstrapOptions options;
    options.discount = sofr.curve;
    const BootstrapResult basis =
        bootstrap(kAsOf,
                  load_quotes(std::string(FIT_EXAMPLES_DIR) + "/usd_term_sofr_3m_basis.csv", kAsOf,
                              lagged_conventions()),
                  us, options);
    CHECK(basis.max_error < 1e-12);
    CHECK(basis.curve->discount(Date(2027, 11, 23)) ==
          doctest::Approx(0.960187844668).epsilon(tol));
    CHECK(basis.curve->discount(Date(2036, 11, 21)) ==
          doctest::Approx(0.668965387433).epsilon(tol));
    CHECK(basis.curve->discount(Date(2056, 11, 22)) ==
          doctest::Approx(0.295884173094).epsilon(tol));
    CHECK(basis.curve->discount(Date(2030, 1, 15)) == doctest::Approx(0.888299994940).epsilon(tol));
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

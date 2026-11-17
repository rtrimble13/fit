#include "fit/core/error.hpp"
#include "fit/curve/bootstrap.hpp"
#include "fit/curve/yield_curve.hpp"

#include <doctest/doctest.h>

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

const InterpolationMethod kAllMethods[] = {
    InterpolationMethod::LinearZero, InterpolationMethod::FlatForward,
    InterpolationMethod::CubicZero, InterpolationMethod::MonotoneCubic};

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
        // Before the first node the zero rate is held flat, except for the monotone cubic,
        // whose forward curve is smooth all the way down to t = 0.
        if (method != InterpolationMethod::MonotoneCubic) {
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
    }
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

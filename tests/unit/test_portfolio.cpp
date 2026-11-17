#include "fit/core/error.hpp"
#include "fit/curve/bootstrap.hpp"
#include "fit/portfolio/portfolio.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <string>

using namespace fit;

namespace {

const Date kAsOf(2026, 11, 17);
const std::string kData = FIT_EXAMPLES_DIR;

CurvePtr example_curve() {
    return bootstrap(kAsOf, load_quotes(kData + "/usd_treasury.csv", kAsOf, {}), Calendar()).curve;
}

} // namespace

TEST_CASE("portfolio: loading positions") {
    const auto positions = load_positions(kData + "/portfolio.csv", Calendar());
    REQUIRE(positions.size() == 7);
    CHECK(positions[0].id == "UST-2Y");
    CHECK(positions[0].face == 25e6);
    CHECK(*positions[0].clean_price == doctest::Approx(99.9375));
    CHECK(positions[4].face == -5e6);
    CHECK_FALSE(positions[5].clean_price.has_value());
    CHECK(positions[5].z_spread == doctest::Approx(0.0085));
    CHECK(positions[5].bond.spec().day_count == DayCount::Thirty360US);
}

TEST_CASE("portfolio: totals are the sum of the positions") {
    const CurvePtr curve = example_curve();
    const auto positions = load_positions(kData + "/portfolio.csv", Calendar());
    const PortfolioValuation v = value_portfolio(positions, kAsOf, curve);

    double mv = 0.0;
    double dv01 = 0.0;
    for (const PositionValuation& p : v.positions) {
        mv += p.market_value;
        dv01 += p.dv01;
        CHECK(p.market_value == doctest::Approx(p.face * p.bond.dirty / 100.0));
        CHECK(std::signbit(p.dv01) == std::signbit(p.face)); // shorts have negative DV01
    }
    CHECK(v.market_value == doctest::Approx(mv));
    CHECK(v.dv01 == doctest::Approx(dv01));
    CHECK(v.duration == doctest::Approx(v.dv01 * 1e4 / v.market_value));

    double key_sum = 0.0;
    for (const double k : v.key_dv01) {
        key_sum += k;
    }
    CHECK(key_sum == doctest::Approx(v.dv01).epsilon(1e-6));

    // Priced positions are marked to market; the rest sit on the curve at their spread.
    CHECK(v.positions[0].bond.clean == doctest::Approx(99.9375));
    CHECK(*v.positions[5].bond.z_spread == doctest::Approx(0.0085));
}

TEST_CASE("portfolio: scenario P&L is consistent with DV01 and convexity") {
    const CurvePtr curve = example_curve();
    const auto positions = load_positions(kData + "/portfolio.csv", Calendar());
    const PortfolioValuation v = value_portfolio(positions, kAsOf, curve);

    CHECK(shocked_market_value(positions, v, curve, ZeroShift()) ==
          doctest::Approx(v.market_value).epsilon(1e-12));
    const double up = shocked_market_value(positions, v, curve, ZeroShift::parallel(1e-4));
    CHECK(up - v.market_value == doctest::Approx(-v.dv01).epsilon(1e-3));

    const double up50 = shocked_market_value(positions, v, curve, ZeroShift::parallel(0.005));
    const double down50 = shocked_market_value(positions, v, curve, ZeroShift::parallel(-0.005));
    CHECK(down50 - v.market_value > v.market_value - up50); // positive convexity
}

TEST_CASE("portfolio: cashflow ladder conserves face and nets shorts") {
    const auto positions = load_positions(kData + "/portfolio.csv", Calendar());
    double principal = 0.0;
    Date previous = kAsOf;
    for (const DatedFlow& f : portfolio_cashflows(positions, kAsOf)) {
        CHECK(f.payment > previous); // strictly increasing: one row per date
        previous = f.payment;
        principal += f.principal;
    }
    CHECK(principal == doctest::Approx(73e6));
}

TEST_CASE("portfolio: an empty or flat book has undefined ratios, not garbage") {
    const PortfolioValuation v = value_portfolio({}, kAsOf, example_curve());
    CHECK(v.market_value == 0.0);
    CHECK(std::isnan(v.duration));
    CHECK(std::isnan(v.yield));
}

TEST_CASE("portfolio: errors identify the position") {
    auto positions = load_positions(kData + "/portfolio.csv", Calendar());
    CHECK_THROWS_WITH_AS(value_portfolio(positions, Date(2029, 1, 1), example_curve()),
                         doctest::Contains("position 'UST-2Y'"), Error);
}

#include "fit/core/error.hpp"
#include "fit/math/interpolation.hpp"
#include "fit/math/solver.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using namespace fit;

namespace {

const std::vector<double> kX{0.1, 0.5, 1, 2, 3.5, 5, 7.2, 10, 20, 30};
const std::vector<double> kY{4.3, 4.2, 4.0, 3.85, 3.9, 3.95, 4.1, 4.25, 4.7, 4.5};

} // namespace

TEST_CASE("solver: Brent finds roots to tolerance") {
    CHECK(brent([](double x) { return x * x - 2.0; }, 0.0, 2.0, "sqrt") ==
          doctest::Approx(std::sqrt(2.0)).epsilon(1e-13));
    CHECK(brent([](double x) { return std::cos(x) - x; }, 0.0, 1.0, "fixed point") ==
          doctest::Approx(0.7390851332151607).epsilon(1e-13));
    CHECK(brent([](double x) { return x - 1.0; }, 1.0, 2.0, "endpoint") == 1.0);
}

TEST_CASE("solver: failures name the problem") {
    CHECK_THROWS_WITH(brent([](double x) { return x * x + 1.0; }, -1.0, 1.0, "yield"),
                      "yield: no solution in the search interval");
    CHECK_THROWS_AS(solve_near([](double x) { return x * x + 1.0; }, 0.0, 0.1, -5.0, 5.0, "z"),
                    Error);
}

TEST_CASE("solver: solve_near expands the bracket") {
    const double root =
        solve_near([](double x) { return std::exp(x) - 20.0; }, 0.0, 0.01, -10.0, 10.0, "exp");
    CHECK(root == doctest::Approx(std::log(20.0)).epsilon(1e-12));
}

TEST_CASE("interpolation: every scheme reproduces its nodes") {
    for (const auto& f : {Interpolation::linear(kX, kY), Interpolation::natural_cubic(kX, kY),
                          Interpolation::monotone_cubic(kX, kY)}) {
        for (std::size_t i = 0; i < kX.size(); ++i) {
            CHECK(f.value(kX[i]) == doctest::Approx(kY[i]).epsilon(1e-14));
        }
    }
}

TEST_CASE("interpolation: natural cubic spline matches SciPy CubicSpline(bc_type='natural')") {
    const auto f = Interpolation::natural_cubic(kX, kY);
    CHECK(f.value(0.3) == doctest::Approx(4.256584551632).epsilon(1e-11));
    CHECK(f.value(2.9) == doctest::Approx(3.870902888385).epsilon(1e-11));
    CHECK(f.value(15.5) == doctest::Approx(4.540878940796).epsilon(1e-11));
    CHECK(f.derivative(0.77) == doctest::Approx(-0.425990561396).epsilon(1e-10));
    CHECK(f.derivative(27.0) == doctest::Approx(-0.033178330193).epsilon(1e-10));
}

TEST_CASE("interpolation: monotone cubic matches SciPy PchipInterpolator") {
    const auto f = Interpolation::monotone_cubic(kX, kY);
    CHECK(f.value(0.3) == doctest::Approx(4.256087570621).epsilon(1e-11));
    CHECK(f.value(2.9) == doctest::Approx(3.875200000000).epsilon(1e-11));
    CHECK(f.value(15.5) == doctest::Approx(4.564017334254).epsilon(1e-11));
    CHECK(f.derivative(1.5) == doctest::Approx(-0.167553191489).epsilon(1e-10));
    CHECK(f.derivative(27.0) == doctest::Approx(-0.028875000000).epsilon(1e-10));
}

TEST_CASE("interpolation: monotone cubic preserves monotonicity where a spline overshoots") {
    const std::vector<double> x{0, 1, 2, 3, 4, 5};
    const std::vector<double> y{0, 0, 0, 1, 1, 1};
    const auto pchip = Interpolation::monotone_cubic(x, y);
    const auto spline = Interpolation::natural_cubic(x, y);
    double spline_min = 0.0;
    for (double t = 0.0; t <= 5.0; t += 0.01) {
        CHECK(pchip.value(t) >= -1e-15);
        CHECK(pchip.value(t) <= 1.0 + 1e-15);
        CHECK(pchip.derivative(t) >= -1e-15);
        spline_min = std::min(spline_min, spline.value(t));
    }
    CHECK(spline_min < -0.01); // the unconstrained spline undershoots
}

TEST_CASE("interpolation: linear extension outside the node range") {
    const auto f = Interpolation::linear({1.0, 2.0}, {10.0, 12.0});
    CHECK(f.value(3.0) == doctest::Approx(14.0));
    CHECK(f.value(0.0) == doctest::Approx(8.0));
    CHECK(f.derivative(5.0) == doctest::Approx(2.0));
}

TEST_CASE("interpolation: invalid nodes are rejected") {
    CHECK_THROWS_AS(Interpolation::linear({1.0}, {1.0}), Error);
    CHECK_THROWS_AS(Interpolation::linear({1.0, 1.0}, {1.0, 2.0}), Error);
    CHECK_THROWS_AS(Interpolation::natural_cubic({1.0, 2.0}, {1.0}), Error);
}

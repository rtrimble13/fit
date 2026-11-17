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

// Evenly spaced points lo, lo + step, ..., up to hi (inclusive to within rounding).
std::vector<double> grid(double lo, double hi, double step) {
    const auto count = static_cast<int>(std::lround((hi - lo) / step));
    std::vector<double> points;
    points.reserve(static_cast<std::size_t>(count) + 1);
    for (int k = 0; k <= count; ++k) {
        points.push_back(lo + step * k);
    }
    return points;
}

// Monotone convex test data: Y(x_0) = 0 and the given secant slopes d_i between the nodes.
std::vector<double> primitive(const std::vector<double>& x, const std::vector<double>& d) {
    std::vector<double> y{0.0};
    for (std::size_t i = 0; i < d.size(); ++i) {
        y.push_back(y.back() + d[i] * (x[i + 1] - x[i]));
    }
    return y;
}

// Uneven nodes with secants (forward rates) under which all four Hagan-West regions occur.
const std::vector<double> kMcX{0, 0.25, 1, 2, 3, 5, 7, 10, 20, 30};
const std::vector<double> kMcD{0.043, 0.041, 0.038, 0.039, 0.045, 0.047, 0.044, 0.050, 0.046};

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
    CHECK_THROWS_AS(MonotoneConvex({1.0}, {1.0}), Error);
    CHECK_THROWS_AS(MonotoneConvex({0.0, 1.0, 1.0}, {0.0, 1.0, 2.0}), Error);
    CHECK_THROWS_AS(MonotoneConvex({0.0, 1.0}, {0.0}), Error);
}

// ---------------------------------------------------------------------------------------------
// Monotone convex (Hagan and West 2006, 2008). The hand calculations use their notation: d is an
// interval's secant, f_i the node values of the derivative, g = f - d with end values g0 and g1,
// and u the position within the interval.

TEST_CASE("monotone convex: matches a hand calculation in each Hagan-West region") {
    // Secants 2%, 3%, 6%, 7% on unit intervals. Node values f_i = (d_i + d_{i+1}) / 2 = 2.5%,
    // 4.5%, 6.5%; ends f_0 = d_1 - (f_1 - d_1) / 2 = 1.75% and f_4 = 7.25%. No bound is active:
    // each lies in [0, 2 min(adjacent secants)]. QuantLib 1.44
    // ConvexMonotoneInterpolation(x, d, 0, 1, false) gives the same derivative to 1e-17.
    const MonotoneConvex mc({0, 1, 2, 3, 4}, {0, 0.02, 0.05, 0.11, 0.18});
    const double tol = 1e-15;

    // [0, 1]: g0 = -0.25%, g1 = 0.5% = -2 g0, region (i): g = g0 (1 - 3u^2), G = g0 (u - u^3).
    CHECK(mc.derivative(0.0) == doctest::Approx(0.0175).epsilon(tol));
    CHECK(mc.derivative(0.5) == doctest::Approx(0.02 - 0.0025 * 0.25).epsilon(tol));
    CHECK(mc.value(0.5) == doctest::Approx(0.01 - 0.0025 * 0.375).epsilon(tol));

    // [1, 2]: g0 = -0.5%, g1 = 1.5% > -2 g0, region (ii), eta = (g1 + 2 g0) / (g1 - g0) = 0.25:
    // f = 2.5% up to t = 1.25, then 2.5% + 2% ((t - 1.25) / 0.75)^2.
    CHECK(mc.derivative(1.1) == doctest::Approx(0.025).epsilon(tol));
    CHECK(mc.derivative(1.625) == doctest::Approx(0.025 + 0.02 * 0.25).epsilon(tol));
    // Y(1.625) = 0.02 + 2.5% * 0.25 + 0.75 * (2.5% * 0.5 + 2% * 0.5^3 / 3) = 0.03625.
    CHECK(mc.value(1.625) == doctest::Approx(0.03625).epsilon(tol));

    // [2, 3]: g0 = -1.5%, 0 < g1 = 0.5% < -g0 / 2, region (iii), eta = 3 g1 / (g1 - g0) = 0.75:
    // f = 6.5% - 2% (1 - (t - 2) / 0.75)^2 up to t = 2.75, then 6.5%.
    CHECK(mc.derivative(2.5) == doctest::Approx(0.065 - 0.02 / 9.0).epsilon(tol));
    CHECK(mc.derivative(2.9) == doctest::Approx(0.065).epsilon(tol));
    // Y(2.5) = 0.05 + 6.5% * 0.5 - 2% * (0.75 / 3) * (1 - (1/3)^3).
    CHECK(mc.value(2.5) == doctest::Approx(0.0825 - 0.02 * 0.25 * 26.0 / 27.0).epsilon(tol));

    // [3, 4]: g0 = -0.5%, g1 = 0.25% = -g0 / 2, region (i): g = g0 (1 - 3u + 1.5u^2), which has
    // zero slope at the last node.
    CHECK(mc.derivative(3.5) == doctest::Approx(0.07 + 0.005 * 0.125).epsilon(tol));
    CHECK(mc.derivative(4.0) == doctest::Approx(0.0725).epsilon(tol));

    // Outside the nodes Y continues linearly with the end values of f.
    CHECK(mc.derivative(6.0) == doctest::Approx(0.0725).epsilon(tol));
    CHECK(mc.value(5.0) == doctest::Approx(0.18 + 0.0725).epsilon(tol));
    CHECK(mc.derivative(-1.0) == doctest::Approx(0.0175).epsilon(tol));
    CHECK(mc.value(-1.0) == doctest::Approx(-0.0175).epsilon(tol));
}

TEST_CASE("monotone convex: positivity bounds keep the derivative non-negative (region iv)") {
    // Secants 6%, 0.5%, 6%. Unbounded, f_1 = f_2 = 3.25% would give the middle interval
    // g0 = g1 = 2.75% and a minimum of d - g0 g1 / (g0 + g1) = 0.5% - 1.375% < 0. Hagan and West
    // bound f_1 and f_2 to 2 min(6%, 0.5%) = 1%, so g0 = g1 = 0.5%: region (iv) with eta = 0.5
    // and minimum 0.5% - 0.25% = 0.25%. The end values use the unbounded interior values:
    // f_0 = f_3 = 6% - (3.25% - 6%) / 2 = 7.375%.
    const MonotoneConvex mc({0, 1, 2, 3}, {0, 0.06, 0.065, 0.125});
    const double tol = 1e-15;
    CHECK(mc.derivative(0.0) == doctest::Approx(0.07375).epsilon(tol));
    CHECK(mc.derivative(1.0) == doctest::Approx(0.01).epsilon(tol));
    CHECK(mc.derivative(1.5) == doctest::Approx(0.0025).epsilon(tol));
    CHECK(mc.derivative(2.0) == doctest::Approx(0.01).epsilon(tol));
    CHECK(mc.derivative(3.0) == doctest::Approx(0.07375).epsilon(tol));
    // On [1, 1.5], f = 0.25% + 0.75% (1 - 2u)^2, so Y(1.25) = 0.06 + 0.25% * 0.25 +
    // 0.75% * (1 - 0.5^3) / 6, and by symmetry Y(1.5) = 0.06 + 0.5% * 0.5.
    CHECK(mc.value(1.25) == doctest::Approx(0.06171875).epsilon(tol));
    CHECK(mc.value(1.5) == doctest::Approx(0.0625).epsilon(tol));
    // [0, 1]: g0 = 1.375%, g1 = -5% < -2 g0, region (ii): flat at 7.375% up to
    // eta = (g1 + 2 g0) / (g1 - g0) = 0.0225 / 0.06375 = 0.353.
    CHECK(mc.derivative(0.3) == doctest::Approx(0.07375).epsilon(tol));
    for (const double t : grid(-1.0, 4.0, 0.001)) {
        CHECK(mc.derivative(t) >= 0.0);
    }
}

TEST_CASE("monotone convex: negative secants use the unconstrained method") {
    // Adding a constant c to every secant moves f and d together and leaves g unchanged, so the
    // result shifts by c as long as no positivity bound is active. Shifting the hand-calculated
    // data by c = -8% makes every secant negative, so no bound applies, and the result must be
    // the original shifted: f - 8% and Y - 8% x.
    const std::vector<double> x{0, 1, 2, 3, 4};
    const MonotoneConvex base(x, {0, 0.02, 0.05, 0.11, 0.18});
    const MonotoneConvex negative(x, {0, 0.02 - 0.08, 0.05 - 0.16, 0.11 - 0.24, 0.18 - 0.32});
    for (const double t : grid(-1.0, 5.0, 0.01)) {
        CAPTURE(t);
        CHECK(negative.derivative(t) == doctest::Approx(base.derivative(t) - 0.08).epsilon(1e-15));
        CHECK(negative.value(t) == doctest::Approx(base.value(t) - 0.08 * t).epsilon(1e-15));
    }

    // Secants of both signs: nodes are reproduced and the derivative is continuous at them.
    const std::vector<double> mx{0, 1, 2, 4, 7};
    const std::vector<double> my = primitive(mx, {0.01, -0.002, 0.003, -0.001});
    const MonotoneConvex mixed(mx, my);
    for (std::size_t i = 0; i < mx.size(); ++i) {
        CHECK(mixed.value(mx[i]) == doctest::Approx(my[i]).epsilon(1e-15));
        CHECK(mixed.derivative(mx[i] - 1e-9) ==
              doctest::Approx(mixed.derivative(mx[i] + 1e-9)).epsilon(1e-10));
    }
}

TEST_CASE("monotone convex: reproduces nodes, constant secants and interior linear forwards") {
    const std::vector<double> y = primitive(kMcX, kMcD);
    const MonotoneConvex mc(kMcX, y);
    for (std::size_t i = 0; i < kMcX.size(); ++i) {
        CHECK(mc.value(kMcX[i]) == doctest::Approx(y[i]).epsilon(1e-15));
    }

    // Equal secants: Y is linear and f constant, inside and outside the nodes.
    const std::vector<double> x{0.5, 1, 3, 4};
    const MonotoneConvex flat(x, primitive(x, {0.04, 0.04, 0.04}));
    for (const double t : grid(0.0, 6.0, 0.05)) {
        CHECK(flat.derivative(t) == doctest::Approx(0.04).epsilon(1e-15));
        CHECK(flat.value(t) == doctest::Approx(0.04 * (t - 0.5)).epsilon(1e-15));
    }

    // Y = 0.01 x + 0.002 x^2 has f = 0.01 + 0.004 x. Each secant is f at its interval's
    // midpoint, so the interior node values are exact, and on interior intervals g1 = -g0:
    // region (i), where g is linear. f is reproduced exactly away from the two end intervals.
    const std::vector<double> qx{0, 0.5, 1.5, 3, 5, 8};
    std::vector<double> qy;
    qy.reserve(qx.size());
    for (const double v : qx) {
        qy.push_back(0.01 * v + 0.002 * v * v);
    }
    const MonotoneConvex quadratic(qx, qy);
    for (const double t : grid(0.5, 5.0, 0.01)) {
        CHECK(quadratic.derivative(t) == doctest::Approx(0.01 + 0.004 * t).epsilon(1e-15));
        CHECK(quadratic.value(t) == doctest::Approx(0.01 * t + 0.002 * t * t).epsilon(1e-15));
    }
}

TEST_CASE("monotone convex: monotone secants give a monotone derivative") {
    // Increasing secants with a jump large enough that the positivity bound is active at
    // x = 0.25: the estimate 0.25 * 1% + 0.75 * 0.1% = 0.325% is cut to 2 * 0.1%.
    const std::vector<double> x{0, 0.25, 1, 2, 5, 10, 30};
    const std::vector<double> rising{0.001, 0.010, 0.012, 0.02, 0.045, 0.05};
    const std::vector<double> falling(rising.rbegin(), rising.rend());
    const MonotoneConvex up(x, primitive(x, rising));
    const MonotoneConvex down(x, primitive(x, falling));
    CHECK(up.derivative(0.25) == doctest::Approx(0.002).epsilon(1e-15));
    double previous_up = up.derivative(-1.0);
    double previous_down = down.derivative(-1.0);
    for (const double t : grid(-1.0, 35.0, 0.001)) {
        CAPTURE(t);
        const double f_up = up.derivative(t);
        const double f_down = down.derivative(t);
        CHECK(f_up >= previous_up - 1e-15);
        CHECK(f_down <= previous_down + 1e-15);
        previous_up = f_up;
        previous_down = f_down;
    }
}

TEST_CASE("monotone convex: the derivative is continuous and the scheme is local") {
    const std::vector<double> y = primitive(kMcX, kMcD);
    const MonotoneConvex mc(kMcX, y);
    // At each node the one-sided values agree: f moves by at most |f'| * 2e-9 across it.
    for (std::size_t i = 1; i + 1 < kMcX.size(); ++i) {
        CAPTURE(kMcX[i]);
        CHECK(mc.derivative(kMcX[i] - 1e-9) ==
              doctest::Approx(mc.derivative(kMcX[i] + 1e-9)).epsilon(1e-10));
    }

    // Moving y_5 changes the secants either side of x_5, hence f_4, f_5 and f_6, and so only the
    // intervals from x_3 to x_7.
    std::vector<double> moved = y;
    moved[5] += 0.001;
    const MonotoneConvex local(kMcX, moved);
    for (const double t : grid(-1.0, 35.0, 0.01)) {
        if (t <= kMcX[3] || t >= kMcX[7]) {
            CAPTURE(t);
            CHECK(local.value(t) == doctest::Approx(mc.value(t)).epsilon(1e-15));
            CHECK(local.derivative(t) == doctest::Approx(mc.derivative(t)).epsilon(1e-15));
        }
    }
    CHECK(local.derivative(4.0) != doctest::Approx(mc.derivative(4.0)).epsilon(1e-6));
}

TEST_CASE("monotone convex: two nodes give a constant derivative") {
    const MonotoneConvex mc({1.0, 3.0}, {0.5, 0.6});
    for (const double t : {0.0, 1.0, 2.2, 3.0, 9.0}) {
        CHECK(mc.derivative(t) == doctest::Approx(0.05).epsilon(1e-15));
        CHECK(mc.value(t) == doctest::Approx(0.5 + 0.05 * (t - 1.0)).epsilon(1e-15));
    }
}

TEST_CASE("monotone convex: Y stays continuous in the inputs where the shape degenerates") {
    // Secants 3%, 3%, 5%: f_1 equals the second secant, so on [1, 2] g0 = 0 while g1 = 1%. As
    // g0 -> 0 the shapes of regions (ii) and (iv) squeeze the whole change in f against x = 2,
    // so the limit is f = 3% on [1, 2) with a step to f_2 = 4% at the node.
    const std::vector<double> x{0, 1, 2, 3};
    const std::vector<double> y{0, 0.03, 0.06, 0.11};
    const MonotoneConvex mc(x, y);
    CHECK(mc.derivative(1.5) == doctest::Approx(0.03).epsilon(1e-15));
    CHECK(mc.derivative(2.0) == doctest::Approx(0.04).epsilon(1e-15));
    CHECK(mc.value(1.5) == doctest::Approx(0.045).epsilon(1e-15));

    // Perturbing y_1 by e moves Y by O(e) everywhere, including just below x = 2, where the
    // breakpoint eta rounds to 1 for the smallest perturbations.
    const double below_node = std::nextafter(2.0, 0.0);
    for (const double e : {1e-6, 1e-9, 1e-12, 1e-15, 1e-18, -1e-6, -1e-12, -1e-18}) {
        CAPTURE(e);
        std::vector<double> perturbed = y;
        perturbed[1] += e;
        const MonotoneConvex p(x, perturbed);
        for (const double t : {0.5, 1.0, 1.25, 1.5, 1.99, 1.999999, below_node, 2.0, 2.5}) {
            CAPTURE(t);
            REQUIRE(std::isfinite(p.value(t)));
            REQUIRE(std::isfinite(p.derivative(t)));
            CHECK(std::fabs(p.value(t) - mc.value(t)) <= 10.0 * std::fabs(e) + 1e-16);
        }
    }
}

TEST_CASE("monotone convex: a breakpoint that rounds onto the node stays finite") {
    // Secants -3%, -3% (to within 1e-17) and -50%, with no positivity bound as they are
    // negative. On [0.7, 3.97] g0 = -2.8e-17 and g1 = -0.36, region (iv), and
    // eta = g1 / (g0 + g1) rounds to exactly 1 in double precision. The point one ulp below
    // the node then has u = 1 exactly (x86-64, no FMA contraction), where the right-hand
    // piece [eta, 1] is empty and its position must not be computed as 0/0.
    const MonotoneConvex mc({0.0, 0.7, 3.968857809839929, 4.968857809839929},
                            {0.0, -0.021000000000000026, -0.1190657342951979, -0.619065734295198});
    const double node = 3.968857809839929;
    const double below = std::nextafter(node, 0.0);
    REQUIRE(std::isfinite(mc.value(below)));
    REQUIRE(std::isfinite(mc.derivative(below)));
    CHECK(mc.value(below) == doctest::Approx(mc.value(node)).epsilon(1e-15));
    CHECK(mc.derivative(below) == doctest::Approx(mc.derivative(node)).epsilon(1e-15));
}

#include "fit/math/solver.hpp"

#include "fit/core/error.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fit {

double brent(const std::function<double(double)>& f, double lo, double hi, const std::string& what,
             const SolverOptions& options) {
    double a = lo;
    double b = hi;
    double fa = f(a);
    double fb = f(b);
    require(std::isfinite(fa) && std::isfinite(fb), what + ": objective is not finite");
    if (fa == 0.0) {
        return a;
    }
    if (fb == 0.0) {
        return b;
    }
    require(fa * fb < 0.0, what + ": no solution in the search interval");

    double c = a;
    double fc = fa;
    double d = b - a;
    double e = d;
    for (int iteration = 0; iteration < options.max_iterations; ++iteration) {
        if (std::fabs(fc) < std::fabs(fb)) {
            a = b;
            b = c;
            c = a;
            fa = fb;
            fb = fc;
            fc = fa;
        }
        const double tol =
            2.0 * std::numeric_limits<double>::epsilon() * std::fabs(b) + 0.5 * options.tolerance;
        const double m = 0.5 * (c - b);
        if (std::fabs(m) <= tol || fb == 0.0) {
            return b;
        }
        if (std::fabs(e) >= tol && std::fabs(fa) > std::fabs(fb)) {
            // Attempt inverse quadratic interpolation (secant when only two points differ).
            const double s = fb / fa;
            double p;
            double q;
            if (a == c) {
                p = 2.0 * m * s;
                q = 1.0 - s;
            } else {
                const double r0 = fa / fc;
                const double r1 = fb / fc;
                p = s * (2.0 * m * r0 * (r0 - r1) - (b - a) * (r1 - 1.0));
                q = (r0 - 1.0) * (r1 - 1.0) * (s - 1.0);
            }
            if (p > 0.0) {
                q = -q;
            } else {
                p = -p;
            }
            if (2.0 * p < std::min(3.0 * m * q - std::fabs(tol * q), std::fabs(e * q))) {
                e = d;
                d = p / q;
            } else {
                d = m;
                e = m;
            }
        } else {
            d = m;
            e = m;
        }
        a = b;
        fa = fb;
        b += std::fabs(d) > tol ? d : (m > 0.0 ? tol : -tol);
        fb = f(b);
        require(std::isfinite(fb), what + ": objective is not finite");
        if ((fb > 0.0) == (fc > 0.0)) {
            c = a;
            fc = fa;
            d = b - a;
            e = d;
        }
    }
    throw Error(what + ": root search did not converge");
}

double solve_near(const std::function<double(double)>& f, double guess, double step, double min,
                  double max, const std::string& what, const SolverOptions& options) {
    double lo = std::max(min, guess - step);
    double hi = std::min(max, guess + step);
    double flo = f(lo);
    double fhi = f(hi);
    while (flo * fhi > 0.0) {
        require(lo > min || hi < max, what + ": no solution in the search interval");
        step *= 2.0;
        if (lo > min) {
            lo = std::max(min, lo - step);
            flo = f(lo);
        }
        if (hi < max) {
            hi = std::min(max, hi + step);
            fhi = f(hi);
        }
    }
    return brent(f, lo, hi, what, options);
}

} // namespace fit

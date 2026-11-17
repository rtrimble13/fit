#pragma once

#include <functional>
#include <string>

namespace fit {

struct SolverOptions {
    double tolerance = 1e-13; ///< absolute tolerance on the root
    int max_iterations = 200;
};

/// Brent's method on a bracketing interval `[lo, hi]` with `f(lo) * f(hi) <= 0`.
/// Throws `Error` (prefixed with `what`) when the root is not bracketed or does not converge.
double brent(const std::function<double(double)>& f, double lo, double hi, const std::string& what,
             const SolverOptions& options = {});

/// Finds a root near `guess`: grows the interval `[guess - step, guess + step]` geometrically
/// within `[min, max]` until it brackets a sign change, then applies Brent's method.
double solve_near(const std::function<double(double)>& f, double guess, double step, double min,
                  double max, const std::string& what, const SolverOptions& options = {});

} // namespace fit

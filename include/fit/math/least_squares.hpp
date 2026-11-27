#pragma once

#include <functional>
#include <limits>
#include <vector>

namespace fit {

/// Residuals r(x) of a least-squares problem; the vector's length is fixed by the problem.
using ResidualFunction = std::function<std::vector<double>(const std::vector<double>&)>;

struct LeastSquaresOptions {
    int max_iterations = 500;
    /// Stop when an accepted step lowers the cost by less than this fraction of it.
    double relative_tolerance = 1e-15;
    /// Optional box bounds; each step is projected onto them. Empty means unbounded.
    std::vector<double> lower;
    std::vector<double> upper;
};

struct LeastSquaresResult {
    std::vector<double> x;
    std::vector<double> residuals;
    double cost = std::numeric_limits<double>::infinity(); ///< sum of squared residuals
    int iterations = 0;                                    ///< accepted steps
};

/// Minimises the sum of squared residuals by Levenberg-Marquardt: each step solves
/// (J'J + lambda diag(J'J)) dx = -J'r, with the Jacobian J by central differences, and the
/// damping lambda falls after a step that lowers the cost and rises after one that does not.
/// It stops when a step lowers the cost by less than `relative_tolerance`, when no damping
/// gives a lower cost (a minimum to working precision), at a zero cost, or after
/// `max_iterations` accepted steps. The start is projected onto the bounds first, each step is
/// projected onto them, and a parameter on a bound that the step would push outwards is held
/// there for that step while the others are solved for. A residual
/// vector that is not finite counts as an infinite cost. Throws fit::Error if the residuals
/// at the start are not finite or the bounds do not match `x0`.
LeastSquaresResult levenberg_marquardt(const ResidualFunction& residuals, std::vector<double> x0,
                                       const LeastSquaresOptions& options = {});

} // namespace fit

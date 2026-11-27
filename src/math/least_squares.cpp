#include "fit/math/least_squares.hpp"

#include "fit/core/error.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace fit {

namespace {

constexpr double kInitialDamping = 1e-3;
constexpr double kMinDamping = 1e-15;
constexpr double kMaxDamping = 1e16;
constexpr double kDampingDown = 1.0 / 3.0;
constexpr double kDampingUp = 4.0;
constexpr double kDifferenceStep = 1e-6; ///< central-difference step, relative to max(1, |x|)

double sum_of_squares(const std::vector<double>& r) {
    double sum = 0.0;
    for (const double v : r) {
        sum += v * v;
    }
    return std::isfinite(sum) ? sum : std::numeric_limits<double>::infinity();
}

// Solves the n x n symmetric system a x = b (row-major) by Cholesky factorisation. Returns
// false when `a` is not positive definite to working precision.
bool cholesky_solve(std::vector<double> a, std::vector<double> b, std::size_t n,
                    std::vector<double>& x) {
    for (std::size_t j = 0; j < n; ++j) {
        double d = a[j * n + j];
        for (std::size_t k = 0; k < j; ++k) {
            d -= a[j * n + k] * a[j * n + k];
        }
        if (!(d > 0.0)) {
            return false;
        }
        a[j * n + j] = std::sqrt(d);
        for (std::size_t i = j + 1; i < n; ++i) {
            double s = a[i * n + j];
            for (std::size_t k = 0; k < j; ++k) {
                s -= a[i * n + k] * a[j * n + k];
            }
            a[i * n + j] = s / a[j * n + j];
        }
    }
    for (std::size_t i = 0; i < n; ++i) { // L y = b
        for (std::size_t k = 0; k < i; ++k) {
            b[i] -= a[i * n + k] * b[k];
        }
        b[i] /= a[i * n + i];
    }
    for (std::size_t i = n; i-- > 0;) { // L' x = y
        for (std::size_t k = i + 1; k < n; ++k) {
            b[i] -= a[k * n + i] * b[k];
        }
        b[i] /= a[i * n + i];
    }
    x = std::move(b);
    return std::all_of(x.begin(), x.end(), [](double v) { return std::isfinite(v); });
}

} // namespace

LeastSquaresResult levenberg_marquardt(const ResidualFunction& residuals, std::vector<double> x0,
                                       const LeastSquaresOptions& options) {
    const std::size_t n = x0.size();
    require(n > 0, "least squares: no parameters");
    require(options.lower.empty() || options.lower.size() == n,
            "least squares: lower bounds do not match the parameters");
    require(options.upper.empty() || options.upper.size() == n,
            "least squares: upper bounds do not match the parameters");
    const auto lower = [&](std::size_t j) {
        return options.lower.empty() ? -std::numeric_limits<double>::infinity() : options.lower[j];
    };
    const auto upper = [&](std::size_t j) {
        return options.upper.empty() ? std::numeric_limits<double>::infinity() : options.upper[j];
    };
    const auto project = [&](std::vector<double>& x) {
        for (std::size_t j = 0; j < n; ++j) {
            x[j] = std::clamp(x[j], lower(j), upper(j));
        }
    };

    LeastSquaresResult result;
    result.x = std::move(x0);
    project(result.x);
    result.residuals = residuals(result.x);
    result.cost = sum_of_squares(result.residuals);
    require(std::isfinite(result.cost), "least squares: the residuals at the start are not finite");
    const std::size_t m = result.residuals.size();

    double damping = kInitialDamping;
    bool converged = false;
    while (!converged && result.iterations < options.max_iterations && result.cost > 0.0) {
        // Central differences, one-sided against a bound.
        std::vector<double> jacobian(m * n);
        for (std::size_t j = 0; j < n; ++j) {
            const double x = result.x[j];
            const double step = kDifferenceStep * std::max(1.0, std::fabs(x));
            const double up = std::min(step, upper(j) - x);
            const double down = std::min(step, x - lower(j));
            std::vector<double> trial = result.x;
            trial[j] = x + up;
            const std::vector<double> r_up = residuals(trial);
            trial[j] = x - down;
            const std::vector<double> r_down = residuals(trial);
            for (std::size_t i = 0; i < m && up + down > 0.0; ++i) {
                jacobian[i * n + j] = (r_up[i] - r_down[i]) / (up + down);
            }
        }
        if (!std::all_of(jacobian.begin(), jacobian.end(),
                         [](double v) { return std::isfinite(v); })) {
            break;
        }
        std::vector<double> normal(n * n, 0.0);
        std::vector<double> gradient(n, 0.0);
        for (std::size_t i = 0; i < m; ++i) {
            for (std::size_t j = 0; j < n; ++j) {
                gradient[j] += jacobian[i * n + j] * result.residuals[i];
                for (std::size_t k = 0; k < n; ++k) {
                    normal[j * n + k] += jacobian[i * n + j] * jacobian[i * n + k];
                }
            }
        }
        double largest_diagonal = 0.0;
        for (std::size_t j = 0; j < n; ++j) {
            largest_diagonal = std::max(largest_diagonal, normal[j * n + j]);
        }

        // A parameter on a bound that the descent direction pushes outwards stays where it is
        // for this step, so the others solve their own system rather than a clipped one.
        std::vector<bool> held(n, false);
        for (std::size_t j = 0; j < n; ++j) {
            held[j] = (result.x[j] <= lower(j) && gradient[j] > 0.0) ||
                      (result.x[j] >= upper(j) && gradient[j] < 0.0);
        }

        // Raise the damping until a step lowers the cost; none at all means a minimum.
        bool accepted = false;
        while (!accepted && damping <= kMaxDamping) {
            std::vector<double> damped = normal;
            for (std::size_t j = 0; j < n; ++j) {
                damped[j * n + j] +=
                    damping * std::max(normal[j * n + j], 1e-12 * largest_diagonal);
            }
            std::vector<double> rhs(n);
            for (std::size_t j = 0; j < n; ++j) {
                rhs[j] = -gradient[j];
                if (held[j]) {
                    for (std::size_t k = 0; k < n; ++k) {
                        damped[j * n + k] = 0.0;
                        damped[k * n + j] = 0.0;
                    }
                    damped[j * n + j] = 1.0;
                    rhs[j] = 0.0;
                }
            }
            std::vector<double> step;
            if (!cholesky_solve(damped, rhs, n, step)) {
                damping *= kDampingUp;
                continue;
            }
            std::vector<double> trial = result.x;
            for (std::size_t j = 0; j < n; ++j) {
                trial[j] += step[j];
            }
            project(trial);
            if (trial == result.x) {
                break; // the step no longer moves any parameter
            }
            std::vector<double> r = residuals(trial);
            const double cost = sum_of_squares(r);
            if (cost < result.cost) {
                const double reduction = (result.cost - cost) / result.cost;
                result.x = std::move(trial);
                result.residuals = std::move(r);
                result.cost = cost;
                ++result.iterations;
                damping = std::max(damping * kDampingDown, kMinDamping);
                accepted = true;
                converged = reduction < options.relative_tolerance;
            } else {
                damping *= kDampingUp;
            }
        }
        if (!accepted) {
            break;
        }
    }
    return result;
}

} // namespace fit

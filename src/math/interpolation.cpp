#include "fit/math/interpolation.hpp"

#include "fit/core/error.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace fit {

namespace {

void validate(const std::vector<double>& x, const std::vector<double>& y) {
    require(x.size() == y.size(), "interpolation: x and y sizes differ");
    require(x.size() >= 2, "interpolation: at least two points are required");
    for (std::size_t i = 1; i < x.size(); ++i) {
        require(x[i] > x[i - 1], "interpolation: abscissae must be strictly increasing");
    }
}

std::vector<double> secants(const std::vector<double>& x, const std::vector<double>& y) {
    std::vector<double> d(x.size() - 1);
    for (std::size_t i = 0; i + 1 < x.size(); ++i) {
        d[i] = (y[i + 1] - y[i]) / (x[i + 1] - x[i]);
    }
    return d;
}

} // namespace

Interpolation::Interpolation(std::vector<double> x, std::vector<double> y,
                             std::vector<double> slopes, bool linear)
    : x_(std::move(x)), y_(std::move(y)), slopes_(std::move(slopes)), linear_(linear) {}

Interpolation Interpolation::linear(std::vector<double> x, std::vector<double> y) {
    validate(x, y);
    return Interpolation(std::move(x), std::move(y), {}, true);
}

Interpolation Interpolation::natural_cubic(std::vector<double> x, std::vector<double> y) {
    validate(x, y);
    const std::size_t n = x.size();
    // Solve the tridiagonal system for second derivatives m[i], with m[0] = m[n-1] = 0.
    std::vector<double> m(n, 0.0);
    if (n > 2) {
        std::vector<double> diag(n, 0.0);
        std::vector<double> rhs(n, 0.0);
        std::vector<double> upper(n, 0.0);
        for (std::size_t i = 1; i + 1 < n; ++i) {
            const double h0 = x[i] - x[i - 1];
            const double h1 = x[i + 1] - x[i];
            const double lower = h0;
            diag[i] = 2.0 * (h0 + h1);
            upper[i] = h1;
            rhs[i] = 6.0 * ((y[i + 1] - y[i]) / h1 - (y[i] - y[i - 1]) / h0);
            if (i > 1) {
                const double w = lower / diag[i - 1];
                diag[i] -= w * upper[i - 1];
                rhs[i] -= w * rhs[i - 1];
            }
        }
        for (std::size_t i = n - 2; i >= 1; --i) {
            m[i] = (rhs[i] - upper[i] * m[i + 1]) / diag[i];
        }
    }
    std::vector<double> slopes(n);
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const double h = x[i + 1] - x[i];
        slopes[i] = (y[i + 1] - y[i]) / h - h * (2.0 * m[i] + m[i + 1]) / 6.0;
    }
    const double h = x[n - 1] - x[n - 2];
    slopes[n - 1] = (y[n - 1] - y[n - 2]) / h + h * (m[n - 2] + 2.0 * m[n - 1]) / 6.0;
    return Interpolation(std::move(x), std::move(y), std::move(slopes), false);
}

Interpolation Interpolation::monotone_cubic(std::vector<double> x, std::vector<double> y) {
    validate(x, y);
    const std::size_t n = x.size();
    const std::vector<double> d = secants(x, y);
    std::vector<double> slopes(n, 0.0);
    if (n == 2) {
        slopes[0] = slopes[1] = d[0];
        return Interpolation(std::move(x), std::move(y), std::move(slopes), false);
    }
    // Interior slopes: weighted harmonic mean of adjacent secants (zero at local extrema).
    for (std::size_t i = 1; i + 1 < n; ++i) {
        if (d[i - 1] * d[i] > 0.0) {
            const double h0 = x[i] - x[i - 1];
            const double h1 = x[i + 1] - x[i];
            const double w0 = 2.0 * h1 + h0;
            const double w1 = h1 + 2.0 * h0;
            slopes[i] = (w0 + w1) / (w0 / d[i - 1] + w1 / d[i]);
        }
    }
    // End slopes: shape-preserving three-point formula.
    const auto end_slope = [](double h0, double h1, double d0, double d1) {
        double s = ((2.0 * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
        if (s * d0 <= 0.0) {
            s = 0.0;
        } else if (d0 * d1 <= 0.0 && std::fabs(s) > 3.0 * std::fabs(d0)) {
            s = 3.0 * d0;
        }
        return s;
    };
    slopes[0] = end_slope(x[1] - x[0], x[2] - x[1], d[0], d[1]);
    slopes[n - 1] = end_slope(x[n - 1] - x[n - 2], x[n - 2] - x[n - 3], d[n - 2], d[n - 3]);
    return Interpolation(std::move(x), std::move(y), std::move(slopes), false);
}

std::size_t Interpolation::segment(double x) const {
    const auto it = std::upper_bound(x_.begin(), x_.end(), x);
    const auto index = static_cast<std::size_t>(it - x_.begin());
    return std::clamp<std::size_t>(index, 1, x_.size() - 1) - 1;
}

double Interpolation::value(double x) const {
    const std::size_t n = x_.size();
    if (x <= x_.front()) {
        const double slope = linear_ ? (y_[1] - y_[0]) / (x_[1] - x_[0]) : slopes_.front();
        return y_.front() + slope * (x - x_.front());
    }
    if (x >= x_.back()) {
        const double slope =
            linear_ ? (y_[n - 1] - y_[n - 2]) / (x_[n - 1] - x_[n - 2]) : slopes_.back();
        return y_.back() + slope * (x - x_.back());
    }
    const std::size_t i = segment(x);
    const double h = x_[i + 1] - x_[i];
    const double t = (x - x_[i]) / h;
    if (linear_) {
        return y_[i] + t * (y_[i + 1] - y_[i]);
    }
    const double t2 = t * t;
    const double t3 = t2 * t;
    return (2.0 * t3 - 3.0 * t2 + 1.0) * y_[i] + (t3 - 2.0 * t2 + t) * h * slopes_[i] +
           (-2.0 * t3 + 3.0 * t2) * y_[i + 1] + (t3 - t2) * h * slopes_[i + 1];
}

double Interpolation::derivative(double x) const {
    const std::size_t n = x_.size();
    if (x <= x_.front()) {
        return linear_ ? (y_[1] - y_[0]) / (x_[1] - x_[0]) : slopes_.front();
    }
    if (x >= x_.back()) {
        return linear_ ? (y_[n - 1] - y_[n - 2]) / (x_[n - 1] - x_[n - 2]) : slopes_.back();
    }
    const std::size_t i = segment(x);
    const double h = x_[i + 1] - x_[i];
    if (linear_) {
        return (y_[i + 1] - y_[i]) / h;
    }
    const double t = (x - x_[i]) / h;
    const double t2 = t * t;
    return (6.0 * t2 - 6.0 * t) * (y_[i] - y_[i + 1]) / h +
           (3.0 * t2 - 4.0 * t + 1.0) * slopes_[i] + (3.0 * t2 - 2.0 * t) * slopes_[i + 1];
}

} // namespace fit

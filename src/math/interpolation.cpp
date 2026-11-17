#include "fit/math/interpolation.hpp"

#include "fit/core/error.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace fit {

namespace {

// Position within the piece [eta, 1] of a monotone convex interval. When eta has rounded to 1
// the piece is empty and u can only be its end point.
double right_piece_position(double eta, double u) {
    return eta < 1.0 ? (u - eta) / (1.0 - eta) : 1.0;
}

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

// ---------------------------------------------------------------------------------------------
// MonotoneConvex
//
// Notation follows Hagan and West (2006, 2008): d_i are the secants (discrete forwards), f_i
// the node values of f (instantaneous forwards at the pillars), and on each interval
// g = f - d with g0 and g1 its values at the two ends.

MonotoneConvex::MonotoneConvex(std::vector<double> x, std::vector<double> y)
    : x_(std::move(x)), y_(std::move(y)) {
    validate(x_, y_);
    const std::size_t n = x_.size() - 1; // number of intervals
    const std::vector<double> d = secants(x_, y_);

    // Node values: linear interpolation between the secants placed at the interval midpoints,
    // f_i = (h_i d_{i+1} + h_{i+1} d_i) / (h_i + h_{i+1}); at the two ends, the values that give
    // f zero slope there. With a single interval f is the secant.
    std::vector<double> f(n + 1, d[0]);
    if (n > 1) {
        for (std::size_t i = 1; i < n; ++i) {
            const double h0 = x_[i] - x_[i - 1];
            const double h1 = x_[i + 1] - x_[i];
            f[i] = (h0 * d[i] + h1 * d[i - 1]) / (h0 + h1);
        }
        f[0] = d[0] - 0.5 * (f[1] - d[0]);
        f[n] = d[n - 1] - 0.5 * (f[n - 1] - d[n - 1]);
    }

    // Positivity: Hagan and West bound each node value to [0, 2 min(adjacent secants)], which
    // keeps f non-negative on any interval whose secant and neighbours are non-negative. The
    // bound is meaningless when an adjacent secant is negative (its upper end would lie below
    // zero), so there the node value is left as estimated; with negative rates the scheme is
    // then the unconstrained method.
    const auto bound = [](double value, double secant) {
        return secant >= 0.0 ? std::clamp(value, 0.0, 2.0 * secant) : value;
    };
    f[0] = bound(f[0], d[0]);
    for (std::size_t i = 1; i < n; ++i) {
        f[i] = bound(f[i], std::min(d[i - 1], d[i]));
    }
    f[n] = bound(f[n], d[n - 1]);

    segments_.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        segments_.push_back(make_segment(x_[i], x_[i + 1] - x_[i], y_[i], d[i], f[i], f[i + 1]));
    }
}

MonotoneConvex::Segment MonotoneConvex::make_segment(double x0, double h, double y0, double d,
                                                     double f0, double f1) {
    Segment s;
    s.x0 = x0;
    s.h = h;
    s.y0 = y0;
    s.d = d;
    s.g0 = f0 - d;
    s.g1 = f1 - d;
    const double g0 = s.g0;
    const double g1 = s.g1;
    // The four regions of Hagan and West; their boundaries are included in region (i). Each
    // shape integrates to zero over the interval, so the mean of f is the secant.
    if ((g0 < 0.0 && -0.5 * g0 <= g1 && g1 <= -2.0 * g0) ||
        (g0 > 0.0 && -0.5 * g0 >= g1 && g1 >= -2.0 * g0)) {
        s.shape = Shape::Quadratic;
    } else if ((g0 < 0.0 && g1 > -2.0 * g0) || (g0 > 0.0 && g1 < -2.0 * g0)) {
        s.shape = Shape::FlatThenCurve;
        s.eta = (g1 + 2.0 * g0) / (g1 - g0);
    } else if ((g0 > 0.0 && 0.0 > g1 && g1 > -0.5 * g0) ||
               (g0 < 0.0 && 0.0 < g1 && g1 < -0.5 * g0)) {
        s.shape = Shape::CurveThenFlat;
        s.eta = 3.0 * g1 / (g1 - g0);
    } else if ((g0 > 0.0 && g1 > 0.0) || (g0 < 0.0 && g1 < 0.0)) {
        s.shape = Shape::Extremum;
        s.eta = g1 / (g0 + g1);
        s.extremum = -g0 * g1 / (g0 + g1);
    } else {
        // g0 or g1 is exactly zero. As either tends to zero, shapes (ii) to (iv) collapse onto
        // g = 0 inside the interval, so this choice keeps Y continuous in the node values.
        s.shape = Shape::Zero;
    }
    return s;
}

// The quadratic pieces are written in terms of their position within the piece, a ratio in
// [0, 1], rather than powers of eta or 1 - eta, which may be arbitrarily small.
double MonotoneConvex::g(const Segment& s, double u) {
    switch (s.shape) {
    case Shape::Zero:
        return 0.0;
    case Shape::Quadratic:
        return s.g0 * (1.0 - 4.0 * u + 3.0 * u * u) + s.g1 * (3.0 * u * u - 2.0 * u);
    case Shape::FlatThenCurve: {
        if (u <= s.eta) {
            return s.g0;
        }
        const double v = right_piece_position(s.eta, u);
        return s.g0 + (s.g1 - s.g0) * v * v;
    }
    case Shape::CurveThenFlat: {
        if (u >= s.eta) {
            return s.g1;
        }
        const double w = 1.0 - u / s.eta;
        return s.g1 + (s.g0 - s.g1) * w * w;
    }
    case Shape::Extremum: {
        if (u < s.eta) {
            const double w = 1.0 - u / s.eta;
            return s.extremum + (s.g0 - s.extremum) * w * w;
        }
        const double v = right_piece_position(s.eta, u);
        return s.extremum + (s.g1 - s.extremum) * v * v;
    }
    }
    return 0.0;
}

double MonotoneConvex::g_integral(const Segment& s, double u) {
    // For a piece (1 - u/eta)^2 on [0, eta]: integral from 0 to u = u (1 - v + v^2/3), v = u/eta.
    // For a piece ((u - eta)/(1 - eta))^2 on [eta, 1]: integral from eta to u = (u - eta) v^2/3.
    switch (s.shape) {
    case Shape::Zero:
        return 0.0;
    case Shape::Quadratic:
        return s.g0 * u * (1.0 - 2.0 * u + u * u) + s.g1 * u * u * (u - 1.0);
    case Shape::FlatThenCurve: {
        if (u <= s.eta) {
            return s.g0 * u;
        }
        const double v = right_piece_position(s.eta, u);
        return s.g0 * u + (s.g1 - s.g0) * (u - s.eta) * v * v / 3.0;
    }
    case Shape::CurveThenFlat: {
        if (u >= s.eta) {
            return s.g1 * u + (s.g0 - s.g1) * s.eta / 3.0;
        }
        const double v = u / s.eta;
        return s.g1 * u + (s.g0 - s.g1) * u * (1.0 - v + v * v / 3.0);
    }
    case Shape::Extremum: {
        if (u < s.eta) {
            const double v = u / s.eta;
            return s.extremum * u + (s.g0 - s.extremum) * u * (1.0 - v + v * v / 3.0);
        }
        const double v = right_piece_position(s.eta, u);
        return s.extremum * u + (s.g0 - s.extremum) * s.eta / 3.0 +
               (s.g1 - s.extremum) * (u - s.eta) * v * v / 3.0;
    }
    }
    return 0.0;
}

std::size_t MonotoneConvex::segment(double x) const {
    const auto it = std::upper_bound(x_.begin(), x_.end(), x);
    const auto index = static_cast<std::size_t>(it - x_.begin());
    return std::clamp<std::size_t>(index, 1, x_.size() - 1) - 1;
}

double MonotoneConvex::value(double x) const {
    if (x <= x_.front()) {
        const Segment& s = segments_.front();
        return y_.front() + (s.d + s.g0) * (x - x_.front());
    }
    if (x >= x_.back()) {
        const Segment& s = segments_.back();
        return y_.back() + (s.d + s.g1) * (x - x_.back());
    }
    const Segment& s = segments_[segment(x)];
    const double u = (x - s.x0) / s.h;
    return s.y0 + s.d * (x - s.x0) + s.h * g_integral(s, u);
}

double MonotoneConvex::derivative(double x) const {
    if (x <= x_.front()) {
        return segments_.front().d + segments_.front().g0;
    }
    if (x >= x_.back()) {
        return segments_.back().d + segments_.back().g1;
    }
    const Segment& s = segments_[segment(x)];
    return s.d + g(s, (x - s.x0) / s.h);
}

} // namespace fit

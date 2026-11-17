#pragma once

#include <vector>

namespace fit {

/// One-dimensional interpolant through strictly increasing abscissae.
///
/// All schemes are represented as piecewise cubic Hermite polynomials (node values plus node
/// slopes), which gives a uniform evaluation path for values and first derivatives.
/// Outside the node range the interpolant is extended linearly using the end slope.
class Interpolation {
public:
    /// Piecewise linear.
    static Interpolation linear(std::vector<double> x, std::vector<double> y);
    /// Natural cubic spline (zero second derivative at both ends).
    static Interpolation natural_cubic(std::vector<double> x, std::vector<double> y);
    /// Monotonicity-preserving cubic (PCHIP, Fritsch-Carlson slopes).
    static Interpolation monotone_cubic(std::vector<double> x, std::vector<double> y);

    double value(double x) const;
    double derivative(double x) const;

    const std::vector<double>& x() const { return x_; }
    const std::vector<double>& y() const { return y_; }

private:
    Interpolation(std::vector<double> x, std::vector<double> y, std::vector<double> slopes,
                  bool linear);
    std::size_t segment(double x) const;

    std::vector<double> x_;
    std::vector<double> y_;
    std::vector<double> slopes_;
    bool linear_ = false;
};

} // namespace fit

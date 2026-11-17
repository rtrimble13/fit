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

/// Hagan-West monotone convex interpolation.
///
/// Interpolates a function Y through nodes (x_i, y_i), i = 0..n, by constructing its derivative
/// f = Y'. On each interval the mean of f equals the secant slope
/// d_i = (y_i - y_{i-1}) / (x_i - x_{i-1}), so Y passes through every node, and f is the
/// piecewise quadratic of Hagan and West. Properties of f:
///
/// - continuous, including at the nodes (see below for the one degenerate exception);
/// - monotone on every interval whose secant lies between the secants of its neighbours;
/// - non-negative on every interval whose secant and neighbouring secants are non-negative;
/// - local: changing y_i changes f only on the two intervals either side of x_i.
///
/// Where two adjacent secants are exactly equal and the next differs, the construction's limit
/// is a step in f at the node where the secants start to differ; Y stays continuous in its
/// inputs, which is what a bootstrap needs.
///
/// Outside [x_0, x_n] Y is extended linearly with the end values of f.
///
/// On a yield curve, Y(t) = -ln DF(t), f is the instantaneous forward rate and d_i the
/// discrete forward rate between pillars.
///
/// References: P. S. Hagan and G. West, "Interpolation methods for curve construction",
/// Applied Mathematical Finance 13(2), 2006; P. S. Hagan and G. West, "Methods for
/// constructing a yield curve", Wilmott Magazine, May 2008.
class MonotoneConvex {
public:
    /// Throws fit::Error unless there are at least two nodes and x is strictly increasing.
    MonotoneConvex(std::vector<double> x, std::vector<double> y);

    /// Y(x).
    double value(double x) const;
    /// f(x) = Y'(x).
    double derivative(double x) const;

private:
    /// The shape of g = f - d over one interval: the four regions of Hagan and West, plus the
    /// degenerate case in which g0 or g1 is zero.
    enum class Shape {
        Zero,          ///< g = 0
        Quadratic,     ///< (i)   g0, g1 of opposite sign with |g1| within [|g0|/2, 2|g0|]
        FlatThenCurve, ///< (ii)  g = g0 up to eta, then a quadratic rising or falling to g1
        CurveThenFlat, ///< (iii) a quadratic from g0 reaching g1 at eta, then g = g1
        Extremum       ///< (iv)  g0, g1 of the same sign: quadratics meeting at eta
    };

    /// One interval's piece of f; u = (x - x0) / h is the position within the interval.
    struct Segment {
        double x0 = 0.0; ///< left node
        double h = 0.0;  ///< interval width
        double y0 = 0.0; ///< Y at the left node
        double d = 0.0;  ///< secant slope: the mean of f over the interval
        double g0 = 0.0; ///< f - d at the left node
        double g1 = 0.0; ///< f - d at the right node
        Shape shape = Shape::Zero;
        double eta = 0.0;      ///< breakpoint in u (shapes ii, iii and iv)
        double extremum = 0.0; ///< g at eta (shape iv)
    };

    static Segment make_segment(double x0, double h, double y0, double d, double f0, double f1);
    /// g(u) and its integral from 0 to u, for u in [0, 1].
    static double g(const Segment& s, double u);
    static double g_integral(const Segment& s, double u);
    std::size_t segment(double x) const;

    std::vector<double> x_;
    std::vector<double> y_;
    std::vector<Segment> segments_;
};

} // namespace fit

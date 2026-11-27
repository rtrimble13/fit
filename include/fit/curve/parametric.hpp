#pragma once

#include "fit/curve/bootstrap.hpp"
#include "fit/curve/yield_curve.hpp"
#include "fit/time/calendar.hpp"
#include "fit/time/date.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fit {

/// A parametric zero curve fitted to quotes by least squares (ADR 0020).
enum class ParametricModel {
    NelsonSiegel, ///< level, slope and one curvature term: four parameters
    Svensson      ///< Nelson-Siegel with a second curvature term: six parameters
};

ParametricModel parse_parametric_model(std::string_view text);
std::string to_string(ParametricModel model);
/// Number of free parameters: 4 for Nelson-Siegel, 6 for Svensson.
int parameter_count(ParametricModel model);

/// Parameters of a Nelson-Siegel(-Svensson) curve. Betas are decimal rates; taus are in years
/// (ACT/365F). `beta3` and `tau2` are used by Svensson only.
struct ParametricParameters {
    double beta0 = 0.0; ///< level: the zero rate as t goes to infinity
    double beta1 = 0.0; ///< slope: beta0 + beta1 is the short rate
    double beta2 = 0.0; ///< first curvature, humped at about tau1
    double beta3 = 0.0; ///< second curvature, humped at about tau2
    double tau1 = 1.0;
    double tau2 = 1.0;
};

/// The continuously compounded ACT/365F zero rate
///
///   z(t) = beta0 + beta1 L(t / tau1) + beta2 C(t / tau1) [+ beta3 C(t / tau2)],
///   L(x) = (1 - exp(-x)) / x,  C(x) = L(x) - exp(-x),
///
/// with z(0) = beta0 + beta1, and DF(t) = exp(-z(t) t). The curve is defined at every
/// maturity, so it needs no extrapolation.
class ParametricCurve final : public YieldCurve {
public:
    /// Throws fit::Error unless the taus it uses are positive and finite.
    ParametricCurve(Date as_of, ParametricModel model, ParametricParameters parameters);

    double discount_t(double t) const override;
    /// The model's zero rate in closed form.
    double zero_t(double t) const;
    /// The model's instantaneous forward rate in closed form:
    /// beta0 + beta1 e^-x + beta2 x e^-x [+ beta3 y e^-y], x = t / tau1, y = t / tau2.
    double forward_t(double t) const;

    ParametricModel model() const { return model_; }
    const ParametricParameters& parameters() const { return parameters_; }

private:
    ParametricModel model_;
    ParametricParameters parameters_;
};

struct FitOptions {
    ParametricModel model = ParametricModel::NelsonSiegel;
    /// As in BootstrapOptions: the curve that discounts the instruments (empty: the fitted
    /// curve itself), and the reference curve of basis quotes (empty: the discount curve).
    CurvePtr discount = nullptr;
    CurvePtr reference = nullptr;
};

struct FitResult {
    std::shared_ptr<const ParametricCurve> curve;
    std::vector<Quote> quotes;   ///< sorted by pillar date
    std::vector<double> implied; ///< model-implied rate of each quote, in the quote's units
    double rmse = 0.0;           ///< root mean square of implied - quote (decimal)
    double max_error = 0.0;      ///< max |implied - quote|
    int iterations = 0;          ///< Levenberg-Marquardt steps from the best start
};

/// Fits `options.model` to `quotes` by least squares on the quotes' own units: the sum over
/// quotes of (implied rate - quoted rate)^2, equally weighted. Implied rates are those of
/// `implied_quote_rate`, so every quote type is priced as in the bootstrap. The fit runs
/// Levenberg-Marquardt from a fixed grid of starting taus and keeps the lowest cost; taus are
/// bounded to [0.05, 50] years. Svensson's taus are not interchangeable (tau1 also shapes the
/// slope term), so they are reported as fitted. Throws fit::Error for fewer quotes than
/// parameters, or for the inputs the bootstrap rejects (a pillar not after `as_of`, a par quote
/// on another discount curve, a basis quote with nothing to price against, a curve with another
/// as-of date). Several quotes may share a pillar.
FitResult fit_curve(Date as_of, std::vector<Quote> quotes, const Calendar& calendar,
                    const FitOptions& options = {});

} // namespace fit

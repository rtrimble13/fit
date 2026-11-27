#include "fit/curve/parametric.hpp"

#include "fit/core/error.hpp"
#include "fit/io/csv.hpp"
#include "fit/math/least_squares.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace fit {

namespace {

constexpr double kMinTau = 0.05; ///< years: bounds on the fitted taus
constexpr double kMaxTau = 50.0;
constexpr double kBp = 1e4;              ///< residuals are fitted in basis points for scaling
constexpr int kFirstPassIterations = 20; ///< steps from each start before the best are refined
constexpr std::size_t kRefined = 3;      ///< starts then refined
constexpr int kRefineIterations = 200;   ///< steps for each refined start

// Records the times a pricer asks a curve for; the discount factors are those of `base`.
class RecordingCurve final : public YieldCurve {
public:
    RecordingCurve(const YieldCurve& base, std::vector<double>& times)
        : YieldCurve(base.as_of()), base_(base), times_(times) {}
    double discount_t(double t) const override {
        times_.push_back(t);
        return base_.discount_t(t);
    }

private:
    const YieldCurve& base_;
    std::vector<double>& times_;
};

// A curve's discount factors at a fixed set of times, computed once: each evaluation of the fit
// asks for the same dates many times over. Any other time falls through to the curve.
class TabulatedCurve final : public YieldCurve {
public:
    TabulatedCurve(const ParametricCurve& curve, const std::vector<double>& times)
        : YieldCurve(curve.as_of()), curve_(curve), times_(times) {
        discounts_.reserve(times.size());
        for (const double t : times) {
            discounts_.push_back(curve.discount_t(t));
        }
    }
    double discount_t(double t) const override {
        const auto it = std::lower_bound(times_.begin(), times_.end(), t);
        return it != times_.end() && *it == t
                   ? discounts_[static_cast<std::size_t>(it - times_.begin())]
                   : curve_.discount_t(t);
    }

private:
    const ParametricCurve& curve_;
    const std::vector<double>& times_;
    std::vector<double> discounts_;
};

// L(x) = (1 - e^-x) / x, with its limit 1 at x = 0.
double loading(double x) {
    constexpr double kSeries = 1e-8;
    return x < kSeries ? 1.0 - x / 2.0 : -std::expm1(-x) / x;
}

// C(x) = L(x) - e^-x, the hump-shaped curvature loading.
double hump(double x) {
    return loading(x) - std::exp(-x);
}

void require_tau(double tau, const char* name) {
    require(std::isfinite(tau) && tau > 0.0, std::string("parametric curve: ") + name +
                                                 " must be positive, not " + std::to_string(tau));
}

} // namespace

ParametricModel parse_parametric_model(std::string_view text) {
    const std::string t = to_lower(text);
    if (t == "nelson-siegel" || t == "ns") {
        return ParametricModel::NelsonSiegel;
    }
    if (t == "svensson" || t == "nss" || t == "nelson-siegel-svensson") {
        return ParametricModel::Svensson;
    }
    throw Error("unknown fit model '" + std::string(text) +
                "' (expected nelson-siegel or svensson)");
}

std::string to_string(ParametricModel model) {
    return model == ParametricModel::NelsonSiegel ? "nelson-siegel" : "svensson";
}

int parameter_count(ParametricModel model) {
    constexpr int kNelsonSiegel = 4;
    constexpr int kSvensson = 6;
    return model == ParametricModel::NelsonSiegel ? kNelsonSiegel : kSvensson;
}

ParametricCurve::ParametricCurve(Date as_of, ParametricModel model, ParametricParameters parameters)
    : YieldCurve(as_of), model_(model), parameters_(parameters) {
    require_tau(parameters_.tau1, "tau1");
    if (model_ == ParametricModel::Svensson) {
        require_tau(parameters_.tau2, "tau2");
    }
}

double ParametricCurve::zero_t(double t) const {
    const ParametricParameters& p = parameters_;
    const double x = std::max(t, 0.0) / p.tau1;
    double z = p.beta0 + p.beta1 * loading(x) + p.beta2 * hump(x);
    if (model_ == ParametricModel::Svensson) {
        z += p.beta3 * hump(std::max(t, 0.0) / p.tau2);
    }
    return z;
}

double ParametricCurve::forward_t(double t) const {
    const ParametricParameters& p = parameters_;
    const double x = std::max(t, 0.0) / p.tau1;
    double f = p.beta0 + p.beta1 * std::exp(-x) + p.beta2 * x * std::exp(-x);
    if (model_ == ParametricModel::Svensson) {
        const double y = std::max(t, 0.0) / p.tau2;
        f += p.beta3 * y * std::exp(-y);
    }
    return f;
}

double ParametricCurve::discount_t(double t) const {
    return t <= 0.0 ? 1.0 : std::exp(-zero_t(t) * t);
}

FitResult fit_curve(Date as_of, std::vector<Quote> quotes, const Calendar& calendar,
                    const FitOptions& options) {
    const ParametricModel model = options.model;
    const auto k = static_cast<std::size_t>(parameter_count(model));
    const std::string what = to_string(model) + " fit: ";
    require(!quotes.empty(), what + "no quotes supplied");
    require(quotes.size() >= k, what + "the model has " + std::to_string(k) +
                                    " parameters, so it needs at least " + std::to_string(k) +
                                    " quotes, not " + std::to_string(quotes.size()));
    for (const auto& [curve, name] :
         {std::pair{options.discount, "discount"}, std::pair{options.reference, "reference"}}) {
        require(!curve || curve->as_of() == as_of,
                what + "the " + name + " curve is as of " +
                    (curve ? curve->as_of().to_string() : std::string()) + ", not " +
                    as_of.to_string());
    }
    std::stable_sort(quotes.begin(), quotes.end(),
                     [](const Quote& a, const Quote& b) { return a.pillar < b.pillar; });
    for (const Quote& q : quotes) {
        require(q.pillar > as_of, "quote '" + q.label + "': maturity is not after the as-of date");
    }

    // x = (beta0, beta1, beta2, [beta3], ln tau1, [ln tau2]).
    const bool svensson = model == ParametricModel::Svensson;
    const std::size_t betas = svensson ? 4 : 3;
    const auto parameters_of = [&](const std::vector<double>& x) {
        ParametricParameters p;
        p.beta0 = x[0];
        p.beta1 = x[1];
        p.beta2 = x[2];
        p.beta3 = svensson ? x[3] : 0.0;
        p.tau1 = std::exp(x[betas]);
        p.tau2 = svensson ? std::exp(x[betas + 1]) : 1.0;
        return p;
    };
    std::vector<QuotePricer> pricers;
    pricers.reserve(quotes.size());
    for (const Quote& q : quotes) {
        pricers.emplace_back(q, calendar);
    }
    const auto implied_rates = [&](const YieldCurve& curve) {
        std::vector<double> implied(quotes.size());
        for (std::size_t i = 0; i < quotes.size(); ++i) {
            implied[i] = pricers[i].implied(curve, options.discount ? *options.discount : curve,
                                            options.reference.get());
        }
        return implied;
    };
    // The times the quotes ask the fitted curve for, from one pricing on a trial curve.
    std::vector<double> times;
    {
        const ParametricCurve trial(as_of, model, ParametricParameters{});
        implied_rates(RecordingCurve(trial, times));
        std::sort(times.begin(), times.end());
        times.erase(std::unique(times.begin(), times.end()), times.end());
    }
    const ResidualFunction residuals = [&](const std::vector<double>& x) {
        const ParametricCurve curve(as_of, model, parameters_of(x));
        std::vector<double> r = implied_rates(TabulatedCurve(curve, times));
        for (std::size_t i = 0; i < r.size(); ++i) {
            r[i] = (r[i] - quotes[i].rate) * kBp;
        }
        return r;
    };

    // Start from the shortest and longest quotes as zero rates (a basis spread on top of its
    // reference curve), with no curvature, over a fixed grid of taus.
    const YieldCurve* reference =
        options.reference ? options.reference.get() : options.discount.get();
    const auto as_rate = [&](const Quote& q) {
        return q.type == QuoteType::Basis && reference != nullptr
                   ? reference->zero_rate(q.pillar) + q.rate
                   : q.rate;
    };
    const double level = as_rate(quotes.back());
    const double slope = as_rate(quotes.front()) - level;
    std::vector<std::vector<double>> taus;
    if (svensson) {
        // Every ordered pair: tau1 also shapes the slope, so the two taus are not symmetric.
        constexpr double kGrid[] = {0.5, 1.0, 2.0, 4.0, 8.0, 16.0};
        for (const double t1 : kGrid) {
            for (const double t2 : kGrid) {
                if (t1 != t2) {
                    taus.push_back({t1, t2});
                }
            }
        }
    } else {
        for (const double t1 : {0.5, 1.0, 2.0, 3.0, 5.0, 8.0, 12.0}) {
            taus.push_back({t1});
        }
    }
    LeastSquaresOptions lsq;
    lsq.lower.assign(k, -std::numeric_limits<double>::infinity());
    lsq.upper.assign(k, std::numeric_limits<double>::infinity());
    for (std::size_t j = betas; j < k; ++j) {
        lsq.lower[j] = std::log(kMinTau);
        lsq.upper[j] = std::log(kMaxTau);
    }
    // A short first pass from every start, then the best few refined to convergence: starts
    // that run into a tau bound would otherwise use up the iteration limit.
    LeastSquaresOptions first_pass = lsq;
    first_pass.max_iterations = kFirstPassIterations;
    std::vector<LeastSquaresResult> candidates;
    for (const std::vector<double>& start : taus) {
        std::vector<double> x(k, 0.0);
        x[0] = level;
        x[1] = slope;
        for (std::size_t j = 0; j < start.size(); ++j) {
            x[betas + j] = std::log(start[j]);
        }
        candidates.push_back(levenberg_marquardt(residuals, std::move(x), first_pass));
    }
    std::stable_sort(
        candidates.begin(), candidates.end(),
        [](const LeastSquaresResult& a, const LeastSquaresResult& b) { return a.cost < b.cost; });
    candidates.resize(std::min(candidates.size(), kRefined));
    LeastSquaresResult best;
    LeastSquaresOptions refine = lsq;
    refine.max_iterations = kRefineIterations;
    for (const LeastSquaresResult& candidate : candidates) {
        LeastSquaresResult r = levenberg_marquardt(residuals, candidate.x, refine);
        r.iterations += candidate.iterations;
        if (r.cost < best.cost) {
            best = std::move(r);
        }
    }

    FitResult result;
    result.curve = std::make_shared<ParametricCurve>(as_of, model, parameters_of(best.x));
    result.implied = implied_rates(*result.curve);
    double squares = 0.0;
    for (std::size_t i = 0; i < quotes.size(); ++i) {
        const double error = result.implied[i] - quotes[i].rate;
        squares += error * error;
        result.max_error = std::max(result.max_error, std::fabs(error));
    }
    result.rmse = std::sqrt(squares / static_cast<double>(quotes.size()));
    result.iterations = best.iterations;
    result.quotes = std::move(quotes);
    return result;
}

} // namespace fit

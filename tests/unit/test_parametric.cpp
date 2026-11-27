#include "fit/core/error.hpp"
#include "fit/curve/bootstrap.hpp"
#include "fit/curve/parametric.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using namespace fit;

namespace {

const Date kAsOf(2026, 11, 17);

// Illustrative parameters of the size the Federal Reserve publishes for its Svensson curve
// (Gurkaynak, Sack and Wright): decimal betas, taus in years.
const ParametricParameters kSvensson{0.0480, -0.0060, -0.0210, 0.0250, 1.80, 9.50};
const ParametricParameters kNelsonSiegel{0.0480, -0.0060, -0.0210, 0.0, 1.80, 1.0};
// The same humps the other way round. Svensson's taus are not interchangeable, since tau1 also
// shapes the slope, so this is a different curve that the fit must find from its own starts.
const ParametricParameters kSvenssonReversed{0.0480, -0.0060, 0.0250, -0.0210, 9.50, 1.80};

std::vector<Quote> treasury_quotes() {
    return load_quotes(std::string(FIT_EXAMPLES_DIR) + "/usd_treasury.csv", kAsOf, {});
}

// Zero quotes at 1 to 30 years read off `curve`, optionally rounded to four decimals in percent
// as the Federal Reserve publishes its fitted yields: a reconstructed data set.
std::vector<Quote> zero_quotes(const YieldCurve& curve, bool rounded) {
    std::vector<Quote> quotes;
    for (int years = 1; years <= 30; ++years) {
        Quote q = make_quote(kAsOf, QuoteType::Zero, std::to_string(years) + "Y", 0.0, {});
        double percent = curve.zero_rate(q.pillar) * 100.0;
        if (rounded) {
            constexpr double kPublished = 1e4; // four decimal places in percent
            percent = std::round(percent * kPublished) / kPublished;
        }
        q.rate = percent / 100.0;
        quotes.push_back(q);
    }
    return quotes;
}

FitResult fit_model(const std::vector<Quote>& quotes, ParametricModel model) {
    FitOptions options;
    options.model = model;
    return fit_curve(kAsOf, quotes, Calendar(), options);
}

} // namespace

TEST_CASE("parametric: model names") {
    CHECK(parse_parametric_model("nelson-siegel") == ParametricModel::NelsonSiegel);
    CHECK(parse_parametric_model("NS") == ParametricModel::NelsonSiegel);
    CHECK(parse_parametric_model("Svensson") == ParametricModel::Svensson);
    CHECK(parse_parametric_model("nss") == ParametricModel::Svensson);
    CHECK(to_string(ParametricModel::NelsonSiegel) == "nelson-siegel");
    CHECK(to_string(ParametricModel::Svensson) == "svensson");
    CHECK(parameter_count(ParametricModel::NelsonSiegel) == 4);
    CHECK(parameter_count(ParametricModel::Svensson) == 6);
    CHECK_THROWS_WITH(parse_parametric_model("spline"),
                      "unknown fit model 'spline' (expected nelson-siegel or svensson)");
}

TEST_CASE("parametric: discount factors match QuantLib") {
    // Reference: QuantLib 1.44 FittedBondDiscountCurve(reference date, fitting method, parameters,
    // max date, Actual365Fixed) with NelsonSiegelFitting (beta0, beta1, beta2, kappa) and
    // SvenssonFitting (beta0..beta3, kappa, kappa1), where kappa = 1 / tau. Agreement 1e-16.
    const ParametricCurve ns(kAsOf, ParametricModel::NelsonSiegel, kNelsonSiegel);
    const ParametricCurve sv(kAsOf, ParametricModel::Svensson, kSvensson);
    const double tol = 1e-14;
    CHECK(ns.discount(Date(2027, 2, 17)) == doctest::Approx(0.989703715138460).epsilon(tol));
    CHECK(ns.discount(Date(2029, 11, 19)) == doctest::Approx(0.889754710675359).epsilon(tol));
    CHECK(ns.discount(Date(2036, 5, 15)) == doctest::Approx(0.664579142898549).epsilon(tol));
    CHECK(ns.discount(Date(2056, 11, 17)) == doctest::Approx(0.248465296886385).epsilon(tol));
    CHECK(sv.discount(Date(2027, 2, 17)) == doctest::Approx(0.989622433887865).epsilon(tol));
    CHECK(sv.discount(Date(2029, 11, 19)) == doctest::Approx(0.881193734535428).epsilon(tol));
    CHECK(sv.discount(Date(2036, 5, 15)) == doctest::Approx(0.624161597787853).epsilon(tol));
    CHECK(sv.discount(Date(2056, 11, 17)) == doctest::Approx(0.204325311977241).epsilon(tol));
}

TEST_CASE("parametric: limits, forwards and validation") {
    const ParametricCurve sv(kAsOf, ParametricModel::Svensson, kSvensson);
    const ParametricParameters& p = sv.parameters();
    // The short rate is beta0 + beta1 and the long rate beta0; the forward agrees at both ends.
    CHECK(sv.zero_t(0.0) == doctest::Approx(p.beta0 + p.beta1).epsilon(1e-15));
    CHECK(sv.zero_t(1e-10) == doctest::Approx(p.beta0 + p.beta1).epsilon(1e-9));
    CHECK(sv.forward_t(0.0) == doctest::Approx(p.beta0 + p.beta1).epsilon(1e-15));
    CHECK(sv.zero_t(1e5) == doctest::Approx(p.beta0).epsilon(1e-4));
    CHECK(sv.discount_t(0.0) == 1.0);
    // At tau1 and tau2: L(1) = 1 - 1/e, C(1) = 1 - 2/e.
    const double e = std::numbers::e;
    const double l = 1.0 - 1.0 / e;
    const double c = 1.0 - 2.0 / e;
    const auto hump = [](double x) { return (1.0 - std::exp(-x)) / x - std::exp(-x); };
    CHECK(sv.zero_t(p.tau1) ==
          doctest::Approx(p.beta0 + p.beta1 * l + p.beta2 * c + p.beta3 * hump(p.tau1 / p.tau2))
              .epsilon(1e-15));
    // The closed-form forward is the derivative of -ln DF.
    for (const double t : {0.25, 1.8, 5.0, 9.5, 30.0}) {
        CAPTURE(t);
        const double h = 1e-5;
        const double numeric = std::log(sv.discount_t(t - h) / sv.discount_t(t + h)) / (2 * h);
        CHECK(sv.forward_t(t) == doctest::Approx(numeric).epsilon(1e-9));
        CHECK(sv.zero_rate_t(t) == doctest::Approx(sv.zero_t(t)).epsilon(1e-14));
    }
    // Nelson-Siegel ignores beta3 and tau2.
    ParametricParameters extra = kNelsonSiegel;
    extra.beta3 = 0.5;
    extra.tau2 = -1.0;
    CHECK(ParametricCurve(kAsOf, ParametricModel::NelsonSiegel, extra).zero_t(3.0) ==
          ParametricCurve(kAsOf, ParametricModel::NelsonSiegel, kNelsonSiegel).zero_t(3.0));
    ParametricParameters bad = kSvensson;
    bad.tau2 = 0.0;
    CHECK_THROWS_AS(ParametricCurve(kAsOf, ParametricModel::Svensson, bad), Error);
    bad.tau1 = std::nan("");
    CHECK_THROWS_AS(ParametricCurve(kAsOf, ParametricModel::NelsonSiegel, bad), Error);
}

TEST_CASE("fit: a reconstructed data set returns its parameters") {
    // Zero yields at 1 to 30 years from known parameters. Exact yields give the parameters back
    // to rounding; yields rounded to four decimals in percent, as published, give them back
    // within the rounding's effect, with an RMSE near the rounding noise 0.01bp / sqrt(12).
    const std::pair<ParametricModel, ParametricParameters> cases[] = {
        {ParametricModel::NelsonSiegel, kNelsonSiegel},
        {ParametricModel::Svensson, kSvensson},
        {ParametricModel::Svensson, kSvenssonReversed}};
    for (const auto& [model, truth] : cases) {
        CAPTURE(to_string(model));
        CAPTURE(truth.tau1);
        const ParametricCurve curve(kAsOf, model, truth);
        const FitResult exact = fit_model(zero_quotes(curve, false), model);
        const ParametricParameters& p = exact.curve->parameters();
        CHECK(exact.rmse < 1e-15);
        CHECK(p.beta0 == doctest::Approx(truth.beta0).epsilon(1e-12));
        CHECK(p.beta1 == doctest::Approx(truth.beta1).epsilon(1e-12));
        CHECK(p.beta2 == doctest::Approx(truth.beta2).epsilon(1e-12));
        CHECK(p.tau1 == doctest::Approx(truth.tau1).epsilon(1e-12));
        if (model == ParametricModel::Svensson) {
            CHECK(p.beta3 == doctest::Approx(truth.beta3).epsilon(1e-12));
            CHECK(p.tau2 == doctest::Approx(truth.tau2).epsilon(1e-12));
        }
        const FitResult rounded = fit_model(zero_quotes(curve, true), model);
        const ParametricParameters& r = rounded.curve->parameters();
        CHECK(rounded.rmse * 1e4 < 0.004);
        CHECK(rounded.max_error * 1e4 < 0.007);
        const double betas[][2] = {{r.beta0, truth.beta0},
                                   {r.beta1, truth.beta1},
                                   {r.beta2, truth.beta2},
                                   {r.beta3, truth.beta3}};
        for (const auto& beta : betas) {
            CHECK(std::fabs(beta[0] - beta[1]) < 1e-4); // 1bp
        }
        CHECK(std::fabs(r.tau1 / truth.tau1 - 1.0) < 2e-3); // 0.2%
        CHECK(std::fabs(r.tau2 / truth.tau2 - 1.0) < 2e-3);
    }
}

TEST_CASE("fit: the example curve matches an independent fit") {
    // Reference: SciPy 1.18 least_squares (trf, x_scale = jac, tolerances 1e-15) from the same
    // grid of starting taus, minimising the bp residuals of the example's deposits and par yields
    // priced by QuantLib 1.44: FittedBondDiscountCurve for the model, Actual360 deposits, and
    // FixedRateLeg coupons on a backward semi-annual schedule with ActualActual(ISMA) accruals
    // and following payment dates for the par yields.
    const std::vector<Quote> quotes = treasury_quotes();
    const FitResult ns = fit_model(quotes, ParametricModel::NelsonSiegel);
    const ParametricParameters& p = ns.curve->parameters();
    CHECK(p.beta0 == doctest::Approx(0.0495581276).epsilon(1e-8));
    CHECK(p.beta1 == doctest::Approx(-0.0051993338).epsilon(1e-7));
    CHECK(p.beta2 == doctest::Approx(-0.0294202493).epsilon(2e-8));
    CHECK(p.tau1 == doctest::Approx(2.08784886).epsilon(1e-8));
    CHECK(ns.rmse * 1e4 == doctest::Approx(3.0778680842).epsilon(1e-10));
    const std::vector<double> ns_residuals{1.725740792, -1.372721189, -1.981355321, 1.95580565,
                                           0.375378642, -1.066919338, -0.264827516, 0.844026843,
                                           1.809671319, -7.496428489, 5.482646322};
    for (std::size_t i = 0; i < quotes.size(); ++i) {
        CAPTURE(ns.quotes[i].label);
        CHECK((ns.implied[i] - ns.quotes[i].rate) * 1e4 ==
              doctest::Approx(ns_residuals[i]).epsilon(1e-5).scale(1.0));
    }
    // Svensson's second curvature is nearly redundant on these quotes, so the minimum lies in a
    // flat valley: the fitted rates and RMSE agree closely, the parameters less so.
    const FitResult sv = fit_model(quotes, ParametricModel::Svensson);
    CHECK(sv.rmse * 1e4 == doctest::Approx(1.8052192599).epsilon(1e-9));
    CHECK(sv.rmse <= ns.rmse);
    const std::vector<double> sv_residuals{1.405609992, -1.495750239, -1.850778893, 2.445663839,
                                           0.872718164, -1.16387738,  -1.541835247, -0.273365325,
                                           3.004887601, -2.665828912, 1.234137199};
    for (std::size_t i = 0; i < quotes.size(); ++i) {
        CAPTURE(sv.quotes[i].label);
        CHECK((sv.implied[i] - sv.quotes[i].rate) * 1e4 ==
              doctest::Approx(sv_residuals[i]).epsilon(2e-4).scale(1.0));
    }
    const ParametricParameters& s = sv.curve->parameters();
    CHECK(s.beta0 == doctest::Approx(0.0219748036).epsilon(1e-6).scale(1.0));
    CHECK(s.beta3 == doctest::Approx(0.0786394286).epsilon(1e-6).scale(1.0));
    CHECK(s.tau1 == doctest::Approx(1.30798384).epsilon(1e-4).scale(1.0));
    CHECK(s.tau2 == doctest::Approx(13.72504391).epsilon(1e-4).scale(1.0));
}

TEST_CASE("fit: projection curves and inputs") {
    QuoteConventions sofr;
    sofr.calendar = Calendar(Calendar::Rules::UsGovernmentBond);
    sofr.settle_days = 2;
    const Calendar us = sofr.calendar;
    // A fitted projection curve prices its irs quotes on the discount curve, like the bootstrap.
    FitOptions options;
    options.model = ParametricModel::NelsonSiegel;
    options.discount =
        bootstrap(kAsOf, load_quotes(std::string(FIT_EXAMPLES_DIR) + "/usd_sofr.csv", kAsOf, sofr),
                  us)
            .curve;
    const FitResult projection = fit_curve(
        kAsOf, load_quotes(std::string(FIT_EXAMPLES_DIR) + "/usd_term_sofr_3m.csv", kAsOf, sofr),
        us, options);
    CHECK(projection.rmse * 1e4 < 5.0);
    for (std::size_t i = 0; i < projection.quotes.size(); ++i) {
        CHECK(projection.implied[i] ==
              implied_quote_rate(projection.quotes[i], *projection.curve, *options.discount, us));
    }
    // Several quotes may share a pillar: a least-squares fit takes both.
    std::vector<Quote> twice = treasury_quotes();
    Quote again = twice[5];
    again.rate += 0.0002;
    twice.push_back(again);
    const FitResult both = fit_model(twice, ParametricModel::NelsonSiegel);
    CHECK(both.quotes.size() == twice.size());
    // Too few quotes, a curve on another date, and maturities on or before the as-of date.
    const std::vector<Quote> three(twice.begin(), twice.begin() + 3);
    CHECK_THROWS_WITH(fit_model(three, ParametricModel::NelsonSiegel),
                      "nelson-siegel fit: the model has 4 parameters, so it needs at least 4 "
                      "quotes, not 3");
    CHECK_THROWS_WITH(fit_model({}, ParametricModel::Svensson), "svensson fit: no quotes supplied");
    options.discount = std::make_shared<InterpolatedCurve>(
        Date(2026, 11, 18), std::vector{1.0}, std::vector{0.04}, InterpolationMethod::FlatForward);
    CHECK_THROWS_WITH(fit_curve(kAsOf, treasury_quotes(), Calendar(), options),
                      "nelson-siegel fit: the discount curve is as of 2026-11-18, not 2026-11-17");
    std::vector<Quote> past = treasury_quotes();
    past[0].pillar = kAsOf;
    CHECK_THROWS_WITH(fit_model(past, ParametricModel::NelsonSiegel),
                      "quote '1M': maturity is not after the as-of date");
}

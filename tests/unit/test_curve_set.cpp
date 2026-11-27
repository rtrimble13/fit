#include "fit/core/error.hpp"
#include "fit/curve/curve_set.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace fit;

namespace {

const Date kAsOf(2026, 11, 17);
const std::string kData = FIT_EXAMPLES_DIR;

QuoteConventions sofr_conventions() {
    QuoteConventions conventions;
    conventions.calendar = Calendar(Calendar::Rules::UsGovernmentBond);
    conventions.settle_days = 2;
    return conventions;
}

CurveSpec spec(std::string name, const std::string& file, std::string discount) {
    CurveSpec s;
    s.name = std::move(name);
    s.quotes = load_quotes(kData + "/" + file, kAsOf, sofr_conventions());
    s.discount = std::move(discount);
    s.calendar = sofr_conventions().calendar;
    return s;
}

// A curve-set file written to the temporary directory for the duration of a test.
struct TempFile {
    explicit TempFile(const std::string& text)
        : path((std::filesystem::temp_directory_path() / "fit_test_curve_set.csv").string()) {
        std::ofstream(path) << text;
    }
    ~TempFile() { std::filesystem::remove(path); }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    std::string path;
};

} // namespace

TEST_CASE("curve set: curves are built after the curve that discounts them") {
    // The projection curve is listed first; it is built second, on the OIS curve.
    const std::vector<NamedCurve> built = build_curve_set(
        kAsOf, {spec("3m", "usd_term_sofr_3m.csv", "ois"), spec("ois", "usd_sofr.csv", "")});
    REQUIRE(built.size() == 2);
    CHECK(built[0].name == "ois");
    CHECK(built[0].discount_name == "ois");
    CHECK(built[1].name == "3m");
    CHECK(built[1].discount_name == "ois");

    // Identical to building the projection curve on the OIS curve directly.
    BootstrapOptions options;
    options.discount = built[0].bootstrap.curve;
    const BootstrapResult direct =
        bootstrap(kAsOf, load_quotes(kData + "/usd_term_sofr_3m.csv", kAsOf, sofr_conventions()),
                  sofr_conventions().calendar, options);
    for (std::size_t i = 0; i < direct.quotes.size(); ++i) {
        CHECK(built[1].bootstrap.curve->zero_rates()[i] ==
              doctest::Approx(direct.curve->zero_rates()[i]).epsilon(1e-15));
    }

    // A curve that names itself as its discount curve discounts itself.
    const std::vector<NamedCurve> self =
        build_curve_set(kAsOf, {spec("ois", "usd_sofr.csv", "ois")});
    CHECK(self[0].discount_name == "ois");
}

TEST_CASE("curve set: invalid sets are rejected with the curves named") {
    CHECK_THROWS_WITH(build_curve_set(kAsOf, {}), "curve set: no curves");
    CHECK_THROWS_WITH(build_curve_set(kAsOf, {spec("", "usd_sofr.csv", "")}),
                      "curve set: a curve has an empty name");
    CHECK_THROWS_WITH(
        build_curve_set(kAsOf, {spec("a", "usd_sofr.csv", ""), spec("a", "usd_sofr.csv", "")}),
        "curve set: curve 'a' is defined twice");
    CHECK_THROWS_WITH(build_curve_set(kAsOf, {spec("a", "usd_sofr.csv", "b")}),
                      "curve set: curve 'a' is discounted on unknown curve 'b' (curves: a)");
    CHECK_THROWS_WITH(
        build_curve_set(kAsOf, {spec("ok", "usd_sofr.csv", ""), spec("a", "usd_sofr.csv", "c"),
                                spec("b", "usd_sofr.csv", "a"), spec("c", "usd_sofr.csv", "b")}),
        "curve set: curves a, b, c depend on each other in a cycle (through their discount or "
        "reference curves)");
    // Bootstrap failures name the curve when there are several, as for a par quote on a
    // projection curve; a lone curve reports the error unchanged.
    CurveSpec par = spec("tsy", "usd_sofr.csv", "ois");
    par.quotes = load_quotes(kData + "/usd_treasury.csv", kAsOf, {});
    CHECK_THROWS_WITH(build_curve_set(kAsOf, {spec("ois", "usd_sofr.csv", ""), par}),
                      "curve 'tsy': quote '1Y': a par bond quote prices on the curve itself, so "
                      "it cannot be used on a curve discounted on another curve");
    CurveSpec empty = spec("x", "usd_sofr.csv", "");
    empty.quotes.clear();
    CHECK_THROWS_WITH(build_curve_set(kAsOf, {empty}), "bootstrap: no quotes supplied");
}

CurveSpec with_reference(CurveSpec s, std::string reference) {
    s.reference = std::move(reference);
    return s;
}

TEST_CASE("curve set: basis curves are built after their reference curve") {
    // The 6M curve is listed first and references the 3M curve, which is built on the OIS curve:
    // the set builds ois, 3m, then 6m.
    const std::vector<NamedCurve> built = build_curve_set(
        kAsOf, {with_reference(spec("6m", "usd_term_sofr_6m.csv", "ois"), "3m"),
                spec("3m", "usd_term_sofr_3m.csv", "ois"), spec("ois", "usd_sofr.csv", "")});
    REQUIRE(built.size() == 3);
    CHECK(built[0].name == "ois");
    CHECK(built[1].name == "3m");
    CHECK(built[2].name == "6m");
    CHECK(built[2].discount_name == "ois");

    // Identical to building the 6M curve on the OIS curve with the 3M curve as reference.
    BootstrapOptions options;
    options.discount = built[0].bootstrap.curve;
    options.reference = built[1].bootstrap.curve;
    const BootstrapResult direct =
        bootstrap(kAsOf, load_quotes(kData + "/usd_term_sofr_6m.csv", kAsOf, sofr_conventions()),
                  sofr_conventions().calendar, options);
    for (std::size_t i = 0; i < direct.quotes.size(); ++i) {
        CHECK(built[2].bootstrap.curve->zero_rates()[i] == direct.curve->zero_rates()[i]);
    }

    // Without a reference the basis quotes price against the discount curve, as with the pair.
    const std::vector<NamedCurve> pair = build_curve_set(
        kAsOf, {spec("ois", "usd_sofr.csv", ""), spec("3m", "usd_term_sofr_3m_basis.csv", "ois")});
    CHECK(pair[1].bootstrap.max_error < 1e-12);
}

TEST_CASE("curve set: reference curves are checked") {
    const CurveSpec ois = spec("ois", "usd_sofr.csv", "");
    const CurveSpec three = spec("3m", "usd_term_sofr_3m.csv", "ois");
    const CurveSpec six = spec("6m", "usd_term_sofr_6m.csv", "ois");
    CHECK_THROWS_WITH(build_curve_set(kAsOf, {ois, with_reference(six, "1m")}),
                      "curve set: curve '6m' names unknown reference curve '1m' (curves: ois, 6m)");
    CHECK_THROWS_WITH(build_curve_set(kAsOf, {ois, with_reference(six, "6m")}),
                      "curve set: curve '6m' cannot be the reference curve of its own basis "
                      "quotes");
    CHECK_THROWS_WITH(
        build_curve_set(
            kAsOf, {ois, three, with_reference(spec("x", "usd_term_sofr_3m.csv", "ois"), "3m")}),
        "curve set: curve 'x' names reference curve '3m' but has no basis quotes");
    // A cycle through a reference: ois is discounted on 6m, whose basis references ois.
    CHECK_THROWS_WITH(
        build_curve_set(kAsOf, {spec("ois", "usd_sofr.csv", "6m"), with_reference(six, "ois")}),
        "curve set: curves ois, 6m depend on each other in a cycle (through their "
        "discount or reference curves)");
    // A curve with basis quotes, no discount curve and no reference has nothing to price
    // against; the error names the curve.
    CHECK_THROWS_WITH(build_curve_set(kAsOf, {ois, spec("6m", "usd_term_sofr_6m.csv", "")}),
                      "curve '6m': quote '1Y': a basis swap prices against a reference curve, "
                      "and this curve has none: build it on a discount curve, the default "
                      "reference, or name a reference curve in a curve set");
}

TEST_CASE("curve set: fitted curves") {
    // A fitted curve is built by the fit and used like any other: a projection curve can be
    // bootstrapped on it.
    CurveSpec ois = spec("ois", "usd_sofr.csv", "");
    ois.fit = ParametricModel::Svensson;
    const std::vector<NamedCurve> built =
        build_curve_set(kAsOf, {spec("3m", "usd_term_sofr_3m.csv", "ois"), ois});
    REQUIRE(built.size() == 2);
    REQUIRE(built[0].fit.has_value());
    if (const auto& fitted = built[0].fit) {
        CHECK(built[0].curve == fitted->curve);
        CHECK(fitted->curve->model() == ParametricModel::Svensson);
    }
    CHECK(built[0].bootstrap.curve == nullptr);
    CHECK_FALSE(built[1].fit.has_value());
    CHECK(built[1].curve == built[1].bootstrap.curve);
    CHECK(built[1].bootstrap.max_error < 1e-12);
    BootstrapOptions options;
    options.discount = built[0].curve;
    const BootstrapResult direct =
        bootstrap(kAsOf, load_quotes(kData + "/usd_term_sofr_3m.csv", kAsOf, sofr_conventions()),
                  sofr_conventions().calendar, options);
    CHECK(built[1].bootstrap.curve->zero_rates() == direct.curve->zero_rates());

    // In a file, `fit` replaces the default and `none` bootstraps; a fitted row takes no
    // interpolation or extrapolation.
    const TempFile file("name,quotes,fit\na," + kData + "/usd_treasury.csv,nelson-siegel\nb," +
                        kData + "/usd_treasury.csv,none\nc," + kData + "/usd_treasury.csv,\n");
    CurveSetDefaults defaults;
    defaults.fit = ParametricModel::Svensson;
    const std::vector<CurveSpec> specs = load_curve_set(file.path, kAsOf, defaults);
    CHECK(specs[0].fit == ParametricModel::NelsonSiegel);
    CHECK_FALSE(specs[1].fit.has_value());
    CHECK(specs[2].fit == ParametricModel::Svensson);
    const TempFile both("name,quotes,fit,interp\na," + kData +
                        "/usd_treasury.csv,svensson,linear-zero\n");
    const std::string message = both.path + ":2: a fitted curve (fit svensson) takes no interp or "
                                            "extrapolation: it is defined at every maturity";
    CHECK_THROWS_WITH(load_curve_set(both.path, kAsOf, {}), message.c_str());
}

TEST_CASE("curve set: the example file resolves paths and per-curve conventions") {
    CurveSetDefaults defaults;
    defaults.method = InterpolationMethod::MonotoneConvex;
    const std::vector<CurveSpec> specs = load_curve_set(kData + "/usd_curves.csv", kAsOf, defaults);
    REQUIRE(specs.size() == 4);
    CHECK(specs[0].name == "sofr");
    CHECK(specs[0].discount.empty());
    CHECK(specs[0].calendar.name() == "us");
    CHECK(specs[0].quotes.front().start == Date(2026, 11, 19)); // spot_lag 2
    CHECK(specs[1].name == "term-sofr-3m");
    CHECK(specs[1].discount == "sofr");
    CHECK(specs[1].reference.empty());
    CHECK(specs[2].name == "term-sofr-6m");
    CHECK(specs[2].discount == "sofr");
    CHECK(specs[2].reference == "term-sofr-3m");
    CHECK(specs[3].name == "treasury");
    CHECK(specs[3].calendar.name() == "weekends");
    CHECK(specs[3].quotes.front().start == kAsOf); // spot_lag 0
    for (const CurveSpec& s : specs) {
        CHECK(s.options.method == InterpolationMethod::MonotoneConvex); // inherited
        CHECK_FALSE(s.options.extrapolation.has_value());
    }
}

TEST_CASE("curve set: rows override the defaults and headers are checked") {
    const TempFile file("name,quotes,interp,extrapolation\n"
                        "a," +
                        kData + "/usd_treasury.csv,linear-zero,flat-forward\nb," + kData +
                        "/usd_treasury.csv,,\n");
    const std::vector<CurveSpec> specs = load_curve_set(file.path, kAsOf, {});
    REQUIRE(specs.size() == 2);
    CHECK(specs[0].options.method == InterpolationMethod::LinearZero);
    CHECK(specs[0].options.extrapolation == Extrapolation::FlatForward);
    CHECK(specs[1].options.method == InterpolationMethod::FlatForward);
    CHECK_FALSE(specs[1].options.extrapolation.has_value());

    const TempFile misspelt("name,quotes,discunt\na," + kData + "/usd_treasury.csv,\n");
    CHECK_THROWS_AS(load_curve_set(misspelt.path, kAsOf, {}), Error);
    const TempFile missing("name,quotes\na," + kData + "/no_such_file.csv\n");
    CHECK_THROWS_AS(load_curve_set(missing.path, kAsOf, {}), Error);
    // A row's payment_lag reaches its swap quotes; others keep the default.
    const TempFile paid("name,quotes,spot_lag,payment_lag\na," + kData + "/usd_sofr.csv,2,2\nb," +
                        kData + "/usd_sofr.csv,2,\n");
    CurveSetDefaults lag_defaults;
    lag_defaults.payment_lag = 1;
    const std::vector<CurveSpec> paid_specs = load_curve_set(paid.path, kAsOf, lag_defaults);
    CHECK(paid_specs[0].quotes.back().payment_lag == 2);
    CHECK(paid_specs[1].quotes.back().payment_lag == 1);
    CHECK(paid_specs[0].quotes.front().payment_lag == 0); // a deposit
    const TempFile lag("name,quotes,spot_lag\na," + kData + "/usd_treasury.csv,-1\n");
    const std::string message = lag.path + ":2: spot_lag must be non-negative";
    CHECK_THROWS_WITH(load_curve_set(lag.path, kAsOf, {}), message.c_str());
}

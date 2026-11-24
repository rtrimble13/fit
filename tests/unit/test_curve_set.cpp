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
        "curve set: curves a, b, c are discounted on each other in a cycle");
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

TEST_CASE("curve set: the example file resolves paths and per-curve conventions") {
    CurveSetDefaults defaults;
    defaults.method = InterpolationMethod::MonotoneConvex;
    const std::vector<CurveSpec> specs = load_curve_set(kData + "/usd_curves.csv", kAsOf, defaults);
    REQUIRE(specs.size() == 3);
    CHECK(specs[0].name == "sofr");
    CHECK(specs[0].discount.empty());
    CHECK(specs[0].calendar.name() == "us");
    CHECK(specs[0].quotes.front().start == Date(2026, 11, 19)); // spot_lag 2
    CHECK(specs[1].name == "term-sofr-3m");
    CHECK(specs[1].discount == "sofr");
    CHECK(specs[2].name == "treasury");
    CHECK(specs[2].calendar.name() == "weekends");
    CHECK(specs[2].quotes.front().start == kAsOf); // spot_lag 0
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
    const TempFile lag("name,quotes,spot_lag\na," + kData + "/usd_treasury.csv,-1\n");
    const std::string message = lag.path + ":2: spot_lag must be non-negative";
    CHECK_THROWS_WITH(load_curve_set(lag.path, kAsOf, {}), message.c_str());
}

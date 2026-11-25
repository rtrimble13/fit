#pragma once

#include "fit/curve/yield_curve.hpp"
#include "fit/time/calendar.hpp"
#include "fit/time/date.hpp"
#include "fit/time/daycount.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fit {

enum class QuoteType {
    Deposit, ///< simply compounded cash deposit / bill yield, or a term-rate fixing
    Swap,    ///< par rate of a fixed vs compounded-overnight swap (OIS)
    Irs,     ///< par rate of a fixed vs term-rate swap (e.g. a 3M index paid quarterly)
    Fra,     ///< forward-rate agreement "AxB": the simple rate from A to B months after spot
    Future,  ///< quarterly IMM interest-rate future (e.g. 3M SOFR): 100 minus the price
    Par,     ///< par yield of a coupon bond priced at 100
    Zero     ///< continuously compounded ACT/365F zero rate
};

QuoteType parse_quote_type(std::string_view text);
std::string to_string(QuoteType type);

/// A market quote with its conventions fully resolved to dates.
struct Quote {
    QuoteType type = QuoteType::Zero;
    std::string label; ///< tenor or maturity as written in the input
    double rate = 0.0; ///< decimal; for a future, the futures rate (100 - price) / 100
    Date start;        ///< accrual start: spot, a FRA's start or a future's IMM date
    Date maturity;     ///< unadjusted maturity
    Date pillar;       ///< curve node date: last payment date of the instrument
    DayCount day_count = DayCount::Act365F;
    int frequency = 1;       ///< fixed-leg / coupon frequency (Swap, Irs and Par only)
    int float_frequency = 0; ///< floating-leg frequency: the term rate's tenor (Irs only)
    double convexity = 0.0;  ///< futures rate minus forward rate, decimal (Future only)
};

/// Conventions applied when resolving quotes; per-row CSV columns override the defaults.
struct QuoteConventions {
    Calendar calendar;
    int settle_days = 0; ///< business days from as-of to accrual start
    BusinessDayConvention convention = BusinessDayConvention::ModifiedFollowing;
};

/// Builds a quote from `tenor_or_date`: a tenor or date ("5Y", "2031-11-17"); for a FRA its
/// "AxB" months ("3x6"); for a future its quarterly IMM code ("Z6" or "Z26": H, M, U or Z and
/// the year). `day_count`, `frequency` and `float_frequency` fall back to the market defaults
/// for the quote type when empty / zero: deposit, FRA and future ACT/360; swap annual ACT/360;
/// irs annual ACT/360 fixed vs quarterly floating; par semi-annual ACT/ACT ICMA; zero ACT/365F.
/// `float_frequency` is valid for irs quotes only, and `convexity` (decimal) for futures only.
Quote make_quote(Date as_of, QuoteType type, const std::string& tenor_or_date, double rate,
                 const QuoteConventions& conventions, const std::string& day_count = "",
                 int frequency = 0, int float_frequency = 0, double convexity = 0.0);

/// Reads quotes from CSV with columns `type,tenor` and `rate` (in percent), or for futures
/// `price` (per 100) with an optional `convexity_bp`; and optional `daycount`, `frequency`,
/// `float_frequency`, `settle_days`.
std::vector<Quote> load_quotes(const std::string& path, Date as_of,
                               const QuoteConventions& conventions);

/// The rate at which `quote`'s instrument prices to par on `curve` (same units as the quote).
double implied_quote_rate(const Quote& quote, const YieldCurve& curve, const Calendar& calendar);

/// The same when `curve` is a projection curve discounted on `discount`: swap floating legs
/// project forwards on `curve` and both swap legs discount on `discount`; deposits and zero
/// quotes depend on `curve` alone. Throws for `par` quotes, which price a bond on the curve
/// itself, unless `discount` is `curve`.
double implied_quote_rate(const Quote& quote, const YieldCurve& curve, const YieldCurve& discount,
                          const Calendar& calendar);

/// Par coupon rate for a bond-like instrument from `start` to `maturity` on `curve`. Coupons
/// accrue between unadjusted dates and are paid on adjusted ones, as bonds do.
double par_rate(const YieldCurve& curve, Date start, Date maturity, int frequency,
                DayCount day_count, const Calendar& calendar,
                BusinessDayConvention convention = BusinessDayConvention::Following);

/// Par fixed rate of a swap from `start` to `maturity` on `curve`, whose floating leg is worth
/// DF(start) - DF(end) (an overnight-indexed swap on the same curve). Fixed periods accrue
/// between business-day adjusted dates under `convention` and are paid at their adjusted end,
/// as ISDA swaps do.
double swap_rate(const YieldCurve& curve, Date start, Date maturity, int frequency,
                 DayCount day_count, const Calendar& calendar,
                 BusinessDayConvention convention = BusinessDayConvention::ModifiedFollowing);

/// Par fixed rate of a swap whose floating leg pays the forward of `projection` over each
/// floating period (`float_frequency` periods a year, on the same adjusted dates as the fixed
/// leg), with both legs discounted on `discount`. Each floating coupon is worth
/// DF(pay) * (P(start) / P(end) - 1), a par coupon. When `projection` and `discount` are the same
/// curve this is the single-curve swap rate above.
double swap_rate(const YieldCurve& projection, const YieldCurve& discount, Date start,
                 Date maturity, int fixed_frequency, DayCount fixed_day_count, int float_frequency,
                 const Calendar& calendar,
                 BusinessDayConvention convention = BusinessDayConvention::ModifiedFollowing);

struct BootstrapOptions {
    InterpolationMethod method = InterpolationMethod::FlatForward;
    double tolerance = 1e-12; ///< maximum absolute repricing error (decimal rate)
    int max_passes = 100;
    /// Beyond the last pillar; empty means the scheme's natural extrapolation. Bootstrap
    /// instruments never pay beyond their own pillar, so this does not change the nodes.
    std::optional<Extrapolation> extrapolation = std::nullopt;
    /// The curve that discounts the instruments: empty for a curve that discounts itself
    /// (single-curve), or an already built discount (OIS) curve, making this bootstrap build a
    /// projection curve (ADR 0016). It must have the same as-of date.
    CurvePtr discount = nullptr;
};

struct BootstrapResult {
    std::shared_ptr<const InterpolatedCurve> curve;
    std::vector<Quote> quotes;   ///< sorted by pillar date; aligned with curve nodes
    std::vector<double> implied; ///< model-implied rate of each quote
    int passes = 0;              ///< bootstrap sweeps performed
    double max_error = 0.0;      ///< max |implied - quote|
};

/// Bootstraps a zero curve that reprices every quote.
///
/// Pillars are solved one at a time with Brent's method; sweeps repeat until all quotes
/// reprice within tolerance, which makes the result exact for non-local interpolation
/// schemes too (ADR-0008).
BootstrapResult bootstrap(Date as_of, std::vector<Quote> quotes, const Calendar& calendar,
                          const BootstrapOptions& options = {});

} // namespace fit

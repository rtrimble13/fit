#pragma once

#include "fit/curve/yield_curve.hpp"
#include "fit/time/calendar.hpp"
#include "fit/time/date.hpp"
#include "fit/time/daycount.hpp"

#include <memory>
#include <string>
#include <vector>

namespace fit {

enum class QuoteType {
    Deposit, ///< simply compounded cash deposit / bill yield
    Swap,    ///< par rate of a fixed-vs-overnight swap discounted on the same curve
    Par,     ///< par yield of a coupon bond priced at 100
    Zero     ///< continuously compounded ACT/365F zero rate
};

QuoteType parse_quote_type(std::string_view text);
std::string to_string(QuoteType type);

/// A market quote with its conventions fully resolved to dates.
struct Quote {
    QuoteType type = QuoteType::Zero;
    std::string label; ///< tenor or maturity as written in the input
    double rate = 0.0; ///< decimal
    Date start;        ///< accrual start (as-of date plus settlement lag)
    Date maturity;     ///< unadjusted maturity
    Date pillar;       ///< curve node date: last payment date of the instrument
    DayCount day_count = DayCount::Act365F;
    int frequency = 1; ///< fixed-leg / coupon frequency (Swap and Par only)
};

/// Conventions applied when resolving quotes; per-row CSV columns override the defaults.
struct QuoteConventions {
    Calendar calendar;
    int settle_days = 0; ///< business days from as-of to accrual start
    BusinessDayConvention convention = BusinessDayConvention::ModifiedFollowing;
};

/// Builds a quote from `tenor_or_date` ("5Y" or "2031-11-17"). `day_count` and `frequency`
/// fall back to the market defaults for the quote type when empty / zero:
/// deposit ACT/360; swap annual ACT/360; par semi-annual ACT/ACT ICMA; zero ACT/365F.
Quote make_quote(Date as_of, QuoteType type, const std::string& tenor_or_date, double rate,
                 const QuoteConventions& conventions, const std::string& day_count = "",
                 int frequency = 0);

/// Reads quotes from CSV with columns `type,tenor,rate` (rate in percent) and optional
/// `daycount`, `frequency`, `settle_days`.
std::vector<Quote> load_quotes(const std::string& path, Date as_of,
                               const QuoteConventions& conventions);

/// The rate at which `quote`'s instrument prices to par on `curve` (same units as the quote).
double implied_quote_rate(const Quote& quote, const YieldCurve& curve, const Calendar& calendar);

/// Par coupon rate for a bond-like instrument from `start` to `maturity` on `curve`.
double par_rate(const YieldCurve& curve, Date start, Date maturity, int frequency,
                DayCount day_count, const Calendar& calendar,
                BusinessDayConvention convention = BusinessDayConvention::Following);

struct BootstrapOptions {
    InterpolationMethod method = InterpolationMethod::FlatForward;
    double tolerance = 1e-12; ///< maximum absolute repricing error (decimal rate)
    int max_passes = 100;
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

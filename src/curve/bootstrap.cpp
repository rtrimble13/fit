#include "fit/curve/bootstrap.hpp"

#include "fit/core/error.hpp"
#include "fit/io/csv.hpp"
#include "fit/math/solver.hpp"
#include "fit/time/schedule.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace fit {

namespace {

constexpr double kMinZero = -0.25;
constexpr double kMaxZero = 2.0;

std::shared_ptr<const InterpolatedCurve> build(Date as_of, const std::vector<double>& times,
                                               const std::vector<double>& zeros,
                                               InterpolationMethod method,
                                               Extrapolation extrapolation) {
    return std::make_shared<InterpolatedCurve>(as_of, times, zeros, method, extrapolation);
}

} // namespace

QuoteType parse_quote_type(std::string_view text) {
    const std::string t = to_lower(text);
    if (t == "deposit" || t == "depo" || t == "cash") {
        return QuoteType::Deposit;
    }
    if (t == "swap" || t == "ois") {
        return QuoteType::Swap;
    }
    if (t == "irs") {
        return QuoteType::Irs;
    }
    if (t == "par" || t == "bond") {
        return QuoteType::Par;
    }
    if (t == "zero") {
        return QuoteType::Zero;
    }
    throw Error("unknown quote type '" + std::string(text) +
                "' (expected deposit, swap, irs, par or zero)");
}

std::string to_string(QuoteType type) {
    switch (type) {
    case QuoteType::Deposit:
        return "deposit";
    case QuoteType::Swap:
        return "swap";
    case QuoteType::Irs:
        return "irs";
    case QuoteType::Par:
        return "par";
    case QuoteType::Zero:
        return "zero";
    }
    return "";
}

Quote make_quote(Date as_of, QuoteType type, const std::string& tenor_or_date, double rate,
                 const QuoteConventions& conventions, const std::string& day_count, int frequency,
                 int float_frequency) {
    Quote q;
    q.type = type;
    q.label = tenor_or_date;
    q.rate = rate;
    q.start = type == QuoteType::Zero
                  ? as_of
                  : conventions.calendar.advance(as_of, conventions.settle_days);
    q.maturity = Date::looks_like(tenor_or_date) ? Date::parse(tenor_or_date)
                                                 : q.start.add(Period::parse(tenor_or_date));
    require(q.maturity > q.start, "quote '" + tenor_or_date + "': maturity " +
                                      q.maturity.to_string() + " is not after its start date " +
                                      q.start.to_string());
    switch (type) {
    case QuoteType::Deposit:
    case QuoteType::Swap:
    case QuoteType::Irs:
        q.day_count = DayCount::Act360;
        q.frequency = 1;
        q.pillar = conventions.calendar.adjust(q.maturity, conventions.convention);
        break;
    case QuoteType::Par:
        q.day_count = DayCount::ActActICMA;
        q.frequency = 2;
        q.pillar = conventions.calendar.adjust(q.maturity, BusinessDayConvention::Following);
        break;
    case QuoteType::Zero:
        q.day_count = DayCount::Act365F;
        q.frequency = 1;
        q.pillar = q.maturity;
        break;
    }
    if (!day_count.empty()) {
        q.day_count = parse_day_count(day_count);
    }
    if (frequency != 0) {
        q.frequency = frequency;
    }
    require(float_frequency == 0 || type == QuoteType::Irs,
            "quote '" + tenor_or_date + "': float_frequency applies to irs quotes only");
    if (type == QuoteType::Irs) {
        constexpr int kQuarterly = 4; // the common 3M term-rate swap
        q.float_frequency = float_frequency != 0 ? float_frequency : kQuarterly;
    }
    require(type != QuoteType::Deposit || q.day_count != DayCount::ActActICMA,
            "quote '" + tenor_or_date + "': act/act (ICMA) is not valid for deposits");
    return q;
}

std::vector<Quote> load_quotes(const std::string& path, Date as_of,
                               const QuoteConventions& conventions) {
    const CsvTable csv = CsvTable::read(path);
    csv.require_columns_in(
        {"type", "tenor", "rate", "daycount", "frequency", "float_frequency", "settle_days"});
    std::vector<Quote> quotes;
    for (std::size_t i = 0; i < csv.size(); ++i) {
        try {
            QuoteConventions row_conventions = conventions;
            if (const auto lag = csv.get(i, "settle_days")) {
                row_conventions.settle_days = parse_int(*lag, "settle_days");
                require(row_conventions.settle_days >= 0, "settle_days must be non-negative");
            }
            const auto frequency = csv.get(i, "frequency");
            const auto float_frequency = csv.get(i, "float_frequency");
            quotes.push_back(
                make_quote(as_of, parse_quote_type(csv.at(i, "type")), csv.at(i, "tenor"),
                           parse_double(csv.at(i, "rate"), "rate") / 100.0, row_conventions,
                           csv.get(i, "daycount").value_or(""),
                           frequency ? parse_int(*frequency, "frequency") : 0,
                           float_frequency ? parse_int(*float_frequency, "float_frequency") : 0));
        } catch (const Error& e) {
            const std::string what = e.what();
            throw Error(what.starts_with(path) ? what : csv.where(i) + ": " + what);
        }
    }
    require(!quotes.empty(), path + ": no quotes found");
    return quotes;
}

double par_rate(const YieldCurve& curve, Date start, Date maturity, int frequency,
                DayCount day_count, const Calendar& calendar, BusinessDayConvention convention) {
    ScheduleSpec spec;
    spec.start = start;
    spec.maturity = maturity;
    spec.frequency = frequency;
    spec.calendar = calendar;
    spec.convention = convention;
    double annuity = 0.0;
    Date last_payment = start;
    for (const SchedulePeriod& p : make_schedule(spec)) {
        const double accrual = year_fraction(day_count, p.accrual_start, p.accrual_end, p.ref_start,
                                             p.ref_end, frequency);
        annuity += accrual * curve.discount(p.payment);
        last_payment = p.payment;
    }
    return (curve.discount(start) - curve.discount(last_payment)) / annuity;
}

double swap_rate(const YieldCurve& curve, Date start, Date maturity, int frequency,
                 DayCount day_count, const Calendar& calendar, BusinessDayConvention convention) {
    return swap_rate(curve, curve, start, maturity, frequency, day_count, frequency, calendar,
                     convention);
}

double swap_rate(const YieldCurve& projection, const YieldCurve& discount, Date start,
                 Date maturity, int fixed_frequency, DayCount fixed_day_count, int float_frequency,
                 const Calendar& calendar, BusinessDayConvention convention) {
    ScheduleSpec spec;
    spec.start = start;
    spec.maturity = maturity;
    spec.frequency = fixed_frequency;
    spec.calendar = calendar;
    spec.convention = convention;
    // Each period accrues from the previous adjusted date (the start date for the first) to
    // its own adjusted end, which is also its payment date.
    double annuity = 0.0;
    Date accrual_start = start;
    for (const SchedulePeriod& p : make_schedule(spec)) {
        const double accrual = year_fraction(fixed_day_count, accrual_start, p.payment, p.ref_start,
                                             p.ref_end, fixed_frequency);
        annuity += accrual * discount.discount(p.payment);
        accrual_start = p.payment;
    }
    if (&projection == &discount) {
        // On one curve the floating coupons telescope to DF(start) - DF(end).
        return (discount.discount(start) - discount.discount(accrual_start)) / annuity;
    }
    spec.frequency = float_frequency;
    double floating = 0.0;
    Date float_start = start;
    for (const SchedulePeriod& p : make_schedule(spec)) {
        floating += discount.discount(p.payment) *
                    (projection.discount(float_start) / projection.discount(p.payment) - 1.0);
        float_start = p.payment;
    }
    return floating / annuity;
}

double implied_quote_rate(const Quote& quote, const YieldCurve& curve, const Calendar& calendar) {
    return implied_quote_rate(quote, curve, curve, calendar);
}

double implied_quote_rate(const Quote& quote, const YieldCurve& curve, const YieldCurve& discount,
                          const Calendar& calendar) {
    switch (quote.type) {
    case QuoteType::Deposit:
        return (curve.discount(quote.start) / curve.discount(quote.pillar) - 1.0) /
               year_fraction(quote.day_count, quote.start, quote.pillar);
    case QuoteType::Swap:
        // Overnight compounding over a fixed period telescopes on the projection curve, so the
        // floating periods are the fixed periods.
        return swap_rate(curve, discount, quote.start, quote.maturity, quote.frequency,
                         quote.day_count, quote.frequency, calendar,
                         BusinessDayConvention::ModifiedFollowing);
    case QuoteType::Irs:
        return swap_rate(curve, discount, quote.start, quote.maturity, quote.frequency,
                         quote.day_count, quote.float_frequency, calendar,
                         BusinessDayConvention::ModifiedFollowing);
    case QuoteType::Par:
        require(&curve == &discount, "quote '" + quote.label +
                                         "': a par bond quote prices on the curve itself, so it "
                                         "cannot be used on a curve discounted on another curve");
        return par_rate(curve, quote.start, quote.maturity, quote.frequency, quote.day_count,
                        calendar, BusinessDayConvention::Following);
    case QuoteType::Zero:
        return curve.zero_rate(quote.pillar);
    }
    return 0.0;
}

BootstrapResult bootstrap(Date as_of, std::vector<Quote> quotes, const Calendar& calendar,
                          const BootstrapOptions& options) {
    require(!quotes.empty(), "bootstrap: no quotes supplied");
    require(!options.discount || options.discount->as_of() == as_of,
            "bootstrap: the discount curve is as of " +
                (options.discount ? options.discount->as_of().to_string() : std::string()) +
                ", not " + as_of.to_string());
    std::stable_sort(quotes.begin(), quotes.end(),
                     [](const Quote& a, const Quote& b) { return a.pillar < b.pillar; });
    std::vector<double> times;
    std::vector<double> zeros;
    for (std::size_t i = 0; i < quotes.size(); ++i) {
        const Quote& q = quotes[i];
        require(q.pillar > as_of, "quote '" + q.label + "': maturity is not after the as-of date");
        require(!options.discount || q.type != QuoteType::Par,
                "quote '" + q.label +
                    "': a par bond quote prices on the curve itself, so it cannot be used on a "
                    "curve discounted on another curve");
        require(i == 0 || q.pillar > quotes[i - 1].pillar,
                "quotes '" + (i > 0 ? quotes[i - 1].label : "") + "' and '" + q.label +
                    "' mature on the same date " + q.pillar.to_string());
        times.push_back((q.pillar - as_of) / 365.0);
        zeros.push_back(q.rate); // the quote itself is a good first guess for the zero rate
    }

    // Instruments discount on the given discount curve, or on the curve being built.
    const auto implied_rate = [&](const Quote& quote, const YieldCurve& curve) {
        return implied_quote_rate(quote, curve, options.discount ? *options.discount : curve,
                                  calendar);
    };
    const auto max_error = [&](const YieldCurve& curve, std::vector<double>* implied) {
        double worst = 0.0;
        for (std::size_t i = 0; i < quotes.size(); ++i) {
            const double model = implied_rate(quotes[i], curve);
            if (implied != nullptr) {
                (*implied)[i] = model;
            }
            worst = std::max(worst, std::fabs(model - quotes[i].rate));
        }
        return worst;
    };

    const Extrapolation extrapolation =
        options.extrapolation.value_or(natural_extrapolation(options.method));
    BootstrapResult result;
    result.implied.resize(quotes.size());
    for (int pass = 1; pass <= options.max_passes; ++pass) {
        for (std::size_t i = 0; i < quotes.size(); ++i) {
            const auto objective = [&](double z) {
                std::vector<double> trial = zeros;
                trial[i] = z;
                // Until a later pillar has been solved once, keep it level with this one so
                // the first sweep behaves like a classic sequential bootstrap.
                if (pass == 1) {
                    for (std::size_t j = i + 1; j < trial.size(); ++j) {
                        trial[j] = z;
                    }
                }
                const InterpolatedCurve curve(as_of, times, trial, options.method, extrapolation);
                return implied_rate(quotes[i], curve) - quotes[i].rate;
            };
            zeros[i] = solve_near(objective, zeros[i], 0.01, kMinZero, kMaxZero,
                                  "bootstrap of quote '" + quotes[i].label + "'",
                                  {.tolerance = 1e-15, .max_iterations = 200});
        }
        result.passes = pass;
        result.curve = build(as_of, times, zeros, options.method, extrapolation);
        result.max_error = max_error(*result.curve, &result.implied);
        if (result.max_error <= options.tolerance) {
            result.quotes = std::move(quotes);
            return result;
        }
    }
    throw Error("bootstrap did not converge: max repricing error " +
                std::to_string(result.max_error * 1e4) + "bp after " +
                std::to_string(options.max_passes) + " passes");
}

} // namespace fit

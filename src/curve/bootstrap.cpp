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
constexpr int kMonthsPerFuture = 3; // quarterly IMM contracts: IMM date to the next one
constexpr int kQuarterly = 4;       // the common 3M term rate's payment frequency

std::string no_reference(const Quote& quote) {
    return "quote '" + quote.label +
           "': a basis swap prices against a reference curve, and this curve has none: build it "
           "on a discount curve, the default reference, or name a reference curve in a curve set";
}

// A swap period's payment date: its adjusted end, or `payment_lag` business days after it.
Date lagged(Date adjusted_end, const ScheduleSpec& spec, int payment_lag) {
    return payment_lag == 0 ? adjusted_end : spec.calendar.advance(adjusted_end, payment_lag);
}

// A swap leg's periods on the schedule of `spec`: each accrues from the previous adjusted date
// (the start date for the first) to its own adjusted end, on `day_count`, and is paid at that end
// or `payment_lag` business days after it.
LegPlan swap_leg(const ScheduleSpec& spec, DayCount day_count, int payment_lag) {
    LegPlan leg;
    Date accrual_start = spec.start;
    for (const SchedulePeriod& p : make_schedule(spec)) {
        leg.starts.push_back(accrual_start);
        leg.ends.push_back(p.payment);
        leg.pays.push_back(lagged(p.payment, spec, payment_lag));
        leg.accruals.push_back(year_fraction(day_count, accrual_start, p.payment, p.ref_start,
                                             p.ref_end, spec.frequency));
        accrual_start = p.payment;
    }
    return leg;
}

// A bond's coupons on the schedule of `spec`: accruing between unadjusted dates and paid on
// adjusted ones.
LegPlan bond_leg(const ScheduleSpec& spec, DayCount day_count) {
    LegPlan leg;
    for (const SchedulePeriod& p : make_schedule(spec)) {
        leg.pays.push_back(p.payment);
        leg.accruals.push_back(year_fraction(day_count, p.accrual_start, p.accrual_end, p.ref_start,
                                             p.ref_end, spec.frequency));
    }
    return leg;
}

// Sum of accrual times DF(payment) over the leg's periods.
double annuity(const LegPlan& leg, const YieldCurve& discount) {
    double value = 0.0;
    for (std::size_t i = 0; i < leg.pays.size(); ++i) {
        value += leg.accruals[i] * discount.discount(leg.pays[i]);
    }
    return value;
}

// The value on `discount` of the leg paying the forward of `projection` over each period: par
// coupons, each DF(pay) * (P(start) / P(end) - 1).
double floating_leg(const LegPlan& leg, const YieldCurve& projection, const YieldCurve& discount) {
    double value = 0.0;
    for (std::size_t i = 0; i < leg.pays.size(); ++i) {
        value += discount.discount(leg.pays[i]) *
                 (projection.discount(leg.starts[i]) / projection.discount(leg.ends[i]) - 1.0);
    }
    return value;
}

// The par fixed rate of a swap with these legs: on one curve without a lag the floating coupons
// telescope to DF(start) - DF(end).
double swap_rate(const LegPlan& fixed, const LegPlan& floating, const YieldCurve& projection,
                 const YieldCurve& discount, Date start, int payment_lag) {
    const double value = annuity(fixed, discount);
    if (&projection == &discount && payment_lag == 0) {
        return (discount.discount(start) - discount.discount(fixed.ends.back())) / value;
    }
    return floating_leg(floating, projection, discount) / value;
}

// The spread on the reference leg that makes a basis swap with these legs fair.
double basis_spread(const LegPlan& reference_leg, const LegPlan& projection_leg,
                    const YieldCurve& projection, const YieldCurve& reference,
                    const YieldCurve& discount) {
    const double spread = annuity(reference_leg, discount);
    const double reference_value = floating_leg(reference_leg, reference, discount);
    return (floating_leg(projection_leg, projection, discount) - reference_value) / spread;
}

ScheduleSpec leg_spec(Date start, Date maturity, int frequency, const Calendar& calendar,
                      BusinessDayConvention convention) {
    ScheduleSpec spec;
    spec.start = start;
    spec.maturity = maturity;
    spec.frequency = frequency;
    spec.calendar = calendar;
    spec.convention = convention;
    return spec;
}

std::string par_on_discount(const Quote& quote) {
    return "quote '" + quote.label +
           "': a par bond quote prices on the curve itself, so it cannot be used on a curve "
           "discounted on another curve";
}

bool pays_swap_legs(QuoteType type) {
    return type == QuoteType::Swap || type == QuoteType::Irs || type == QuoteType::Basis;
}

// "AxB" for a FRA from A to B months after spot.
std::pair<int, int> parse_fra(const std::string& text) {
    const auto x = text.find_first_of("xX");
    const auto months = [&](std::string_view part) {
        return parse_int(part, "FRA months in '" + text + "'");
    };
    require(x != std::string::npos && x > 0 && x + 1 < text.size(),
            "FRA '" + text + "': expected start and end months such as 3x6");
    const int a = months(std::string_view(text).substr(0, x));
    const int b = months(std::string_view(text).substr(x + 1));
    require(a >= 0 && b > a, "FRA '" + text + "': end month must follow the start month");
    return {a, b};
}

// A quarterly IMM code: H, M, U or Z (March, June, September, December) and the year's last one
// or two digits. One digit means the first year from the as-of year on that ends in it.
std::pair<int, int> parse_future_code(const std::string& code, Date as_of) {
    const std::string usage = "futures contract '" + code +
                              "': expected a quarterly code such as Z6 or Z26 (H Mar, M Jun, "
                              "U Sep, Z Dec, then the year)";
    require(code.size() == 2 || code.size() == 3, usage);
    int month = 0;
    switch (code[0]) {
    case 'H':
    case 'h':
        month = 3;
        break;
    case 'M':
    case 'm':
        month = 6;
        break;
    case 'U':
    case 'u':
        month = 9;
        break;
    case 'Z':
    case 'z':
        month = 12;
        break;
    default:
        throw Error(usage);
    }
    for (std::size_t i = 1; i < code.size(); ++i) {
        require(code[i] >= '0' && code[i] <= '9', usage);
    }
    const int digits = parse_int(std::string_view(code).substr(1), "year");
    if (code.size() == 3) {
        return {2000 + digits, month};
    }
    int year = as_of.year();
    while (year % 10 != digits) {
        ++year;
    }
    return {year, month};
}

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
    if (t == "basis") {
        return QuoteType::Basis;
    }
    if (t == "fra") {
        return QuoteType::Fra;
    }
    if (t == "future" || t == "futures") {
        return QuoteType::Future;
    }
    if (t == "par" || t == "bond") {
        return QuoteType::Par;
    }
    if (t == "zero") {
        return QuoteType::Zero;
    }
    throw Error("unknown quote type '" + std::string(text) +
                "' (expected deposit, swap, irs, basis, fra, future, par or zero)");
}

std::string to_string(QuoteType type) {
    switch (type) {
    case QuoteType::Deposit:
        return "deposit";
    case QuoteType::Swap:
        return "swap";
    case QuoteType::Irs:
        return "irs";
    case QuoteType::Fra:
        return "fra";
    case QuoteType::Future:
        return "future";
    case QuoteType::Basis:
        return "basis";
    case QuoteType::Par:
        return "par";
    case QuoteType::Zero:
        return "zero";
    }
    return "";
}

Quote make_quote(Date as_of, QuoteType type, const std::string& tenor_or_date, double rate,
                 const QuoteConventions& conventions, const std::string& day_count, int frequency,
                 int float_frequency, double convexity) {
    Quote q;
    q.type = type;
    q.label = tenor_or_date;
    q.rate = rate;
    const Date spot = conventions.calendar.advance(as_of, conventions.settle_days);
    if (type == QuoteType::Fra) {
        // As QuantLib's FraRateHelper: start A months after spot, end B - A months after that,
        // each adjusted.
        const auto [a, b] = parse_fra(tenor_or_date);
        q.start = conventions.calendar.adjust(spot.add_months(a), conventions.convention);
        q.maturity = q.start.add_months(b - a);
    } else if (type == QuoteType::Future) {
        // The reference period runs from the contract month's IMM date to the next one.
        const auto [year, month] = parse_future_code(tenor_or_date, as_of);
        q.start = third_wednesday(year, month);
        const Date next = q.start.add_months(kMonthsPerFuture);
        q.maturity = third_wednesday(next.year(), next.month());
        require(q.start >= as_of, "futures contract '" + tenor_or_date + "': its period starts " +
                                      q.start.to_string() +
                                      ", before the as-of date; a contract already in its "
                                      "period needs past fixings, which fit does not take");
    } else {
        q.start = type == QuoteType::Zero ? as_of : spot;
        q.maturity = Date::looks_like(tenor_or_date) ? Date::parse(tenor_or_date)
                                                     : q.start.add(Period::parse(tenor_or_date));
    }
    require(q.maturity > q.start, "quote '" + tenor_or_date + "': maturity " +
                                      q.maturity.to_string() + " is not after its start date " +
                                      q.start.to_string());
    switch (type) {
    case QuoteType::Deposit:
    case QuoteType::Swap:
    case QuoteType::Irs:
    case QuoteType::Fra:
        q.day_count = DayCount::Act360;
        q.frequency = 1;
        q.pillar = conventions.calendar.adjust(q.maturity, conventions.convention);
        break;
    case QuoteType::Future:
        q.day_count = DayCount::Act360;
        q.frequency = 1;
        q.pillar = q.maturity; // IMM dates are used as they are
        break;
    case QuoteType::Basis:
        q.day_count = DayCount::Act360;
        q.frequency = kQuarterly; // the reference leg
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
    require(float_frequency == 0 || type == QuoteType::Irs || type == QuoteType::Basis,
            "quote '" + tenor_or_date + "': float_frequency applies to irs and basis quotes only");
    if (type == QuoteType::Irs || type == QuoteType::Basis) {
        q.float_frequency = float_frequency != 0 ? float_frequency : kQuarterly;
    }
    // Swap legs pay after a lag; the pillar is then the last payment date.
    require(conventions.payment_lag >= 0,
            "quote '" + tenor_or_date + "': the payment lag must be non-negative");
    if (pays_swap_legs(type) && conventions.payment_lag > 0) {
        q.payment_lag = conventions.payment_lag;
        q.pillar = conventions.calendar.advance(q.pillar, q.payment_lag);
    }
    require(convexity == 0.0 || type == QuoteType::Future,
            "quote '" + tenor_or_date + "': a convexity adjustment applies to futures only");
    q.convexity = convexity;
    require((type != QuoteType::Deposit && type != QuoteType::Fra && type != QuoteType::Future) ||
                q.day_count != DayCount::ActActICMA,
            "quote '" + tenor_or_date + "': act/act (ICMA) is not valid for " + to_string(type) +
                " quotes");
    return q;
}

std::vector<Quote> load_quotes(const std::string& path, Date as_of,
                               const QuoteConventions& conventions) {
    const CsvTable csv = CsvTable::read(path);
    csv.require_columns_in({"type", "tenor", "rate", "price", "convexity_bp", "spread_bp",
                            "daycount", "frequency", "float_frequency", "settle_days",
                            "payment_lag"});
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
            const QuoteType type = parse_quote_type(csv.at(i, "type"));
            if (const auto lag = csv.get(i, "payment_lag")) {
                require(pays_swap_legs(type),
                        "the payment_lag column applies to swap, irs and basis quotes only");
                row_conventions.payment_lag = parse_int(*lag, "payment_lag");
            }
            // Futures are quoted by price, with an optional convexity adjustment in bp; basis
            // swaps by their spread in bp; every other quote by its rate in percent.
            const auto price = csv.get(i, "price");
            const auto convexity_bp = csv.get(i, "convexity_bp");
            const auto spread_bp = csv.get(i, "spread_bp");
            require(!spread_bp || type == QuoteType::Basis,
                    "the spread_bp column applies to basis quotes only");
            double rate = 0.0;
            double convexity = 0.0;
            if (type == QuoteType::Basis) {
                if (!spread_bp || csv.get(i, "rate")) {
                    throw Error("a basis swap is quoted by its spread (column spread_bp), not by "
                                "a rate");
                }
                require(!price && !convexity_bp,
                        "the price and convexity_bp columns apply to future quotes only");
                rate = parse_double(*spread_bp, "spread_bp") / 1e4;
            } else if (type == QuoteType::Future) {
                if (!price || csv.get(i, "rate")) {
                    throw Error("a future is quoted by its price (column price), not by a rate");
                }
                rate = (100.0 - parse_double(*price, "price")) / 100.0;
                if (convexity_bp) {
                    convexity = parse_double(*convexity_bp, "convexity_bp") / 1e4;
                }
            } else {
                require(!price && !convexity_bp,
                        "the price and convexity_bp columns apply to future quotes only");
                rate = parse_double(csv.at(i, "rate"), "rate") / 100.0;
            }
            quotes.push_back(make_quote(
                as_of, type, csv.at(i, "tenor"), rate, row_conventions,
                csv.get(i, "daycount").value_or(""),
                frequency ? parse_int(*frequency, "frequency") : 0,
                float_frequency ? parse_int(*float_frequency, "float_frequency") : 0, convexity));
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
    const LegPlan coupons =
        bond_leg(leg_spec(start, maturity, frequency, calendar, convention), day_count);
    return (curve.discount(start) - curve.discount(coupons.pays.back())) / annuity(coupons, curve);
}

double swap_rate(const YieldCurve& curve, Date start, Date maturity, int frequency,
                 DayCount day_count, const Calendar& calendar, BusinessDayConvention convention,
                 int payment_lag) {
    return swap_rate(curve, curve, start, maturity, frequency, day_count, frequency, calendar,
                     convention, payment_lag);
}

double swap_rate(const YieldCurve& projection, const YieldCurve& discount, Date start,
                 Date maturity, int fixed_frequency, DayCount fixed_day_count, int float_frequency,
                 const Calendar& calendar, BusinessDayConvention convention, int payment_lag) {
    ScheduleSpec spec = leg_spec(start, maturity, fixed_frequency, calendar, convention);
    const LegPlan fixed = swap_leg(spec, fixed_day_count, payment_lag);
    spec.frequency = float_frequency;
    return swap_rate(
        fixed,
        float_frequency == fixed_frequency ? fixed : swap_leg(spec, fixed_day_count, payment_lag),
        projection, discount, start, payment_lag);
}

double basis_spread(const YieldCurve& projection, const YieldCurve& reference,
                    const YieldCurve& discount, Date start, Date maturity, int reference_frequency,
                    int projection_frequency, DayCount day_count, const Calendar& calendar,
                    BusinessDayConvention convention, int payment_lag) {
    ScheduleSpec spec = leg_spec(start, maturity, reference_frequency, calendar, convention);
    const LegPlan reference_leg = swap_leg(spec, day_count, payment_lag);
    spec.frequency = projection_frequency;
    return basis_spread(reference_leg, swap_leg(spec, day_count, payment_lag), projection,
                        reference, discount);
}

double implied_quote_rate(const Quote& quote, const YieldCurve& curve, const Calendar& calendar) {
    return implied_quote_rate(quote, curve, curve, calendar);
}

double implied_quote_rate(const Quote& quote, const YieldCurve& curve, const YieldCurve& discount,
                          const Calendar& calendar, const YieldCurve* reference) {
    return QuotePricer(quote, calendar).implied(curve, discount, reference);
}

QuotePricer::QuotePricer(Quote quote, const Calendar& calendar) : quote_(std::move(quote)) {
    const Quote& q = quote_;
    ScheduleSpec spec = leg_spec(q.start, q.maturity, q.frequency, calendar,
                                 BusinessDayConvention::ModifiedFollowing);
    switch (q.type) {
    case QuoteType::Swap:
        // Overnight compounding over a fixed period telescopes on the projection curve, so the
        // floating periods are the fixed periods.
        first_ = swap_leg(spec, q.day_count, q.payment_lag);
        second_ = first_;
        break;
    case QuoteType::Irs:
    case QuoteType::Basis:
        first_ = swap_leg(spec, q.day_count, q.payment_lag); // fixed or reference leg
        spec.frequency = q.float_frequency;
        second_ = swap_leg(spec, q.day_count, q.payment_lag); // this curve's index
        break;
    case QuoteType::Par:
        spec.convention = BusinessDayConvention::Following;
        first_ = bond_leg(spec, q.day_count);
        break;
    case QuoteType::Deposit:
    case QuoteType::Fra:
    case QuoteType::Future:
    case QuoteType::Zero:
        break;
    }
}

double QuotePricer::implied(const YieldCurve& curve, const YieldCurve& discount,
                            const YieldCurve* reference) const {
    const Quote& quote = quote_;
    switch (quote.type) {
    case QuoteType::Deposit:
    case QuoteType::Fra:
        // The simple forward of the curve over the period: a deposit or a term-rate fixing.
        return (curve.discount(quote.start) / curve.discount(quote.pillar) - 1.0) /
               year_fraction(quote.day_count, quote.start, quote.pillar);
    case QuoteType::Future:
        // Overnight compounding over the period telescopes on the curve to the same simple
        // forward; the futures rate is that forward plus the convexity adjustment.
        return (curve.discount(quote.start) / curve.discount(quote.pillar) - 1.0) /
                   year_fraction(quote.day_count, quote.start, quote.pillar) +
               quote.convexity;
    case QuoteType::Swap:
    case QuoteType::Irs:
        return swap_rate(first_, second_, curve, discount, quote.start, quote.payment_lag);
    case QuoteType::Basis: {
        // The reference leg pays the reference curve's index, by default the discount curve's.
        const YieldCurve* other =
            reference != nullptr ? reference : (&discount != &curve ? &discount : nullptr);
        if (other == nullptr) {
            throw Error(no_reference(quote));
        }
        return basis_spread(first_, second_, curve, *other, discount);
    }
    case QuoteType::Par:
        if (&curve != &discount) {
            throw Error(par_on_discount(quote));
        }
        return (curve.discount(quote.start) - curve.discount(first_.pays.back())) /
               annuity(first_, curve);
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
    require(!options.reference || options.reference->as_of() == as_of,
            "bootstrap: the reference curve is as of " +
                (options.reference ? options.reference->as_of().to_string() : std::string()) +
                ", not " + as_of.to_string());
    // Basis quotes project their reference leg on the reference curve, by default the discount
    // curve.
    const YieldCurve* reference =
        options.reference ? options.reference.get() : options.discount.get();
    std::stable_sort(quotes.begin(), quotes.end(),
                     [](const Quote& a, const Quote& b) { return a.pillar < b.pillar; });
    std::vector<double> times;
    std::vector<double> zeros;
    for (std::size_t i = 0; i < quotes.size(); ++i) {
        const Quote& q = quotes[i];
        require(q.pillar > as_of, "quote '" + q.label + "': maturity is not after the as-of date");
        require(!options.discount || q.type != QuoteType::Par, par_on_discount(q));
        require(i == 0 || q.pillar > quotes[i - 1].pillar,
                "quotes '" + (i > 0 ? quotes[i - 1].label : "") + "' and '" + q.label +
                    "' mature on the same date " + q.pillar.to_string());
        require(q.type != QuoteType::Basis || reference != nullptr, no_reference(q));
        times.push_back((q.pillar - as_of) / 365.0);
        // The quote itself is a good first guess for the zero rate; for a basis swap, the
        // reference curve's zero rate plus the spread.
        zeros.push_back(q.type == QuoteType::Basis ? reference->zero_rate(q.pillar) + q.rate
                                                   : q.rate);
    }

    // Instruments discount on the given discount curve, or on the curve being built. Each
    // quote's schedules are resolved once.
    std::vector<QuotePricer> pricers;
    pricers.reserve(quotes.size());
    for (const Quote& q : quotes) {
        pricers.emplace_back(q, calendar);
    }
    const auto implied_rate = [&](std::size_t i, const YieldCurve& curve) {
        return pricers[i].implied(curve, options.discount ? *options.discount : curve,
                                  options.reference.get());
    };
    const auto max_error = [&](const YieldCurve& curve, std::vector<double>* implied) {
        double worst = 0.0;
        for (std::size_t i = 0; i < quotes.size(); ++i) {
            const double model = implied_rate(i, curve);
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
                return implied_rate(i, curve) - quotes[i].rate;
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

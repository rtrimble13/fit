#include "fit/bond/bond.hpp"

#include "fit/core/error.hpp"

#include <algorithm>
#include <utility>

namespace fit {

namespace {

// With no dated date the schedule is rolled back far enough to cover any settlement date.
constexpr int kAssumedLifeYears = 100;

} // namespace

Bond::Bond(BondSpec spec) : spec_(std::move(spec)) {
    const std::string name = spec_.id.empty() ? "bond" : "bond '" + spec_.id + "'";
    require(spec_.coupon >= 0.0, name + ": coupon must be non-negative");
    require(spec_.redemption > 0.0, name + ": redemption must be positive");
    require(spec_.settle_days >= 0, name + ": settlement days must be non-negative");
    if (spec_.issue) {
        require(*spec_.issue < spec_.maturity, name + ": issue date " + spec_.issue->to_string() +
                                                   " must precede maturity " +
                                                   spec_.maturity.to_string());
    }

    ScheduleSpec schedule;
    schedule.maturity = spec_.maturity;
    schedule.frequency = spec_.frequency;
    schedule.calendar = spec_.calendar;
    schedule.convention = spec_.convention;
    schedule.end_of_month = spec_.end_of_month;
    const bool eom = spec_.end_of_month && spec_.maturity.is_end_of_month();
    const int first_year = std::max(1901, spec_.maturity.year() - kAssumedLifeYears);
    const int months_back = (spec_.maturity.year() - first_year) * 12;
    schedule.start = spec_.issue ? *spec_.issue : spec_.maturity.add_months(-months_back, eom);
    try {
        periods_ = make_schedule(schedule);
    } catch (const Error& e) {
        throw Error(name + ": " + e.what());
    }

    cashflows_.reserve(periods_.size());
    for (const SchedulePeriod& p : periods_) {
        Cashflow cf;
        cf.payment = p.payment;
        cf.accrual_start = p.accrual_start;
        cf.accrual_end = p.accrual_end;
        cf.coupon = 100.0 * spec_.coupon *
                    year_fraction(spec_.day_count, p.accrual_start, p.accrual_end, p.ref_start,
                                  p.ref_end, spec_.frequency);
        cashflows_.push_back(cf);
    }
    cashflows_.back().principal = spec_.redemption;
}

Date Bond::settlement_date(Date trade_date) const {
    return spec_.calendar.advance(trade_date, spec_.settle_days);
}

std::size_t Bond::first_cashflow_after(Date settle) const {
    for (std::size_t i = 0; i < cashflows_.size(); ++i) {
        if (cashflows_[i].payment > settle) {
            return i;
        }
    }
    throw Error("bond" + (spec_.id.empty() ? std::string() : " '" + spec_.id + "'") +
                " has no cashflows after " + settle.to_string() + " (matures " +
                spec_.maturity.to_string() + ")");
}

double Bond::accrued(Date settle) const {
    for (const SchedulePeriod& p : periods_) {
        if (settle >= p.accrual_start && settle < p.accrual_end) {
            return 100.0 * spec_.coupon *
                   year_fraction(spec_.day_count, p.accrual_start, settle, p.ref_start, p.ref_end,
                                 spec_.frequency);
        }
    }
    return 0.0; // before the dated date or at/after maturity
}

} // namespace fit

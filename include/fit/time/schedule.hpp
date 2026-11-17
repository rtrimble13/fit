#pragma once

#include "fit/time/calendar.hpp"
#include "fit/time/date.hpp"

#include <vector>

namespace fit {

/// One accrual period of a coupon schedule.
struct SchedulePeriod {
    Date accrual_start; ///< unadjusted
    Date accrual_end;   ///< unadjusted
    Date ref_start;     ///< start of the regular (quasi-)coupon period containing the accrual
    Date ref_end;       ///< end of that regular period (== accrual_end)
    Date payment;       ///< accrual_end adjusted for business days

    bool is_regular() const { return accrual_start == ref_start; }
};

struct ScheduleSpec {
    Date start;        ///< effective / dated date
    Date maturity;     ///< unadjusted maturity
    int frequency = 2; ///< periods per year: 1, 2, 4 or 12
    Calendar calendar; ///< payment calendar
    BusinessDayConvention convention = BusinessDayConvention::Following;
    bool end_of_month = true; ///< roll on month ends when the maturity is a month end
};

/// Generates accrual periods backward from maturity, so any irregular period is a short
/// first stub. Accrual dates are unadjusted; only payment dates are business-day adjusted.
std::vector<SchedulePeriod> make_schedule(const ScheduleSpec& spec);

} // namespace fit

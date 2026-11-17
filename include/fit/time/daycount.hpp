#pragma once

#include "fit/time/date.hpp"

#include <string>
#include <string_view>

namespace fit {

enum class DayCount {
    Act360,      ///< actual/360
    Act365F,     ///< actual/365 fixed
    Thirty360US, ///< 30/360 US (bond basis, SIA end-of-February rules)
    ThirtyE360,  ///< 30E/360 (Eurobond basis)
    ActActISDA,  ///< actual/actual ISDA (split by calendar year)
    ActActICMA   ///< actual/actual ICMA (relative to the coupon reference period)
};

/// Accepts the usual spellings, e.g. `act/360`, `30/360`, `act/act`, `act/act-isda`.
/// Plain `act/act` means ICMA, the government bond convention.
DayCount parse_day_count(std::string_view text);
std::string to_string(DayCount day_count);

/// Number of days between two dates under the convention's day-counting rule.
int day_count_days(DayCount day_count, Date start, Date end);

/// Year fraction between `start` and `end`.
///
/// ACT/ACT ICMA is defined relative to a coupon period, so this overload throws for it; use
/// the reference-period overload instead.
double year_fraction(DayCount day_count, Date start, Date end);

/// Year fraction with an explicit reference (quasi-)coupon period `[ref_start, ref_end]` of
/// regular length `1/frequency` years. Only ACT/ACT ICMA uses the reference period;
/// `[start, end]` must lie inside it.
double year_fraction(DayCount day_count, Date start, Date end, Date ref_start, Date ref_end,
                     int frequency);

} // namespace fit

#include "fit/time/schedule.hpp"

#include "fit/core/error.hpp"

#include <algorithm>

namespace fit {

std::vector<SchedulePeriod> make_schedule(const ScheduleSpec& spec) {
    require(spec.frequency == 1 || spec.frequency == 2 || spec.frequency == 4 ||
                spec.frequency == 12,
            "unsupported frequency " + std::to_string(spec.frequency) +
                " (expected 1, 2, 4 or 12 periods per year)");
    require(spec.start < spec.maturity, "schedule start " + spec.start.to_string() +
                                            " must precede maturity " + spec.maturity.to_string());

    const int step = 12 / spec.frequency;
    const bool eom = spec.end_of_month && spec.maturity.is_end_of_month();

    // Each date is computed directly from maturity so day-of-month clamping never compounds.
    std::vector<SchedulePeriod> periods;
    for (int k = 0;; ++k) {
        const Date end = spec.maturity.add_months(-k * step, eom);
        const Date regular_start = spec.maturity.add_months(-(k + 1) * step, eom);
        SchedulePeriod p;
        p.accrual_end = end;
        p.ref_end = end;
        p.ref_start = regular_start;
        p.accrual_start = std::max(regular_start, spec.start);
        p.payment = spec.calendar.adjust(end, spec.convention);
        periods.push_back(p);
        if (regular_start <= spec.start) {
            break;
        }
    }
    std::reverse(periods.begin(), periods.end());
    return periods;
}

} // namespace fit

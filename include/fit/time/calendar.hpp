#pragma once

#include "fit/time/date.hpp"

#include <set>
#include <string>
#include <string_view>

namespace fit {

enum class BusinessDayConvention { Unadjusted, Following, ModifiedFollowing, Preceding };

BusinessDayConvention parse_business_day_convention(std::string_view text);
std::string to_string(BusinessDayConvention convention);

/// A business-day calendar: a holiday rule set plus optional explicit holidays.
class Calendar {
public:
    enum class Rules {
        None,            ///< every day is a business day
        WeekendsOnly,    ///< Saturday and Sunday are non-business days
        UsGovernmentBond ///< weekends + rule-based US government bond market holidays
    };

    Calendar() = default;
    explicit Calendar(Rules rules) : rules_(rules) {}

    /// Accepts `none`, `weekends`, `us` (alias `us-govt`).
    static Calendar from_name(std::string_view name);
    std::string name() const;

    /// Adds an explicit holiday on top of the rule set.
    void add_holiday(Date date) { extra_holidays_.insert(date); }
    /// Loads explicit holidays from a text file with one ISO date per line (`#` comments).
    void load_holidays(const std::string& path);

    bool is_business_day(Date date) const;
    bool is_holiday(Date date) const { return !is_business_day(date); }

    Date adjust(Date date, BusinessDayConvention convention) const;
    /// Moves `date` forward (or backward for negative values) by whole business days.
    Date advance(Date date, int business_days) const;

private:
    Rules rules_ = Rules::WeekendsOnly;
    std::set<Date> extra_holidays_;
};

/// Western (Gregorian) Easter Sunday for `year`.
Date easter_sunday(int year);

} // namespace fit

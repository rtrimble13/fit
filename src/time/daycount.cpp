#include "fit/time/daycount.hpp"

#include "fit/core/error.hpp"

#include <algorithm>
#include <cctype>

namespace fit {

namespace {

bool is_last_day_of_february(Date d) {
    return d.month() == 2 && d.is_end_of_month();
}

int thirty_360_days(int y1, int m1, int d1, int y2, int m2, int d2) {
    return 360 * (y2 - y1) + 30 * (m2 - m1) + (d2 - d1);
}

} // namespace

DayCount parse_day_count(std::string_view text) {
    std::string t(text);
    std::transform(t.begin(), t.end(), t.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    t.erase(std::remove(t.begin(), t.end(), ' '), t.end());
    if (t == "act/360" || t == "actual/360" || t == "a/360") {
        return DayCount::Act360;
    }
    if (t == "act/365" || t == "act/365f" || t == "actual/365" || t == "a/365" || t == "a/365f") {
        return DayCount::Act365F;
    }
    if (t == "30/360" || t == "30u/360" || t == "30/360us" || t == "bond") {
        return DayCount::Thirty360US;
    }
    if (t == "30e/360" || t == "eurobond") {
        return DayCount::ThirtyE360;
    }
    if (t == "act/act-isda" || t == "act/actisda" || t == "actual/actual-isda") {
        return DayCount::ActActISDA;
    }
    if (t == "act/act" || t == "act/act-icma" || t == "act/acticma" || t == "actual/actual" ||
        t == "actual/actual-icma" || t == "act/act-isma") {
        return DayCount::ActActICMA;
    }
    throw Error("unknown day count '" + std::string(text) +
                "' (expected act/360, act/365, 30/360, 30e/360, act/act or act/act-isda)");
}

std::string to_string(DayCount day_count) {
    switch (day_count) {
    case DayCount::Act360:
        return "act/360";
    case DayCount::Act365F:
        return "act/365";
    case DayCount::Thirty360US:
        return "30/360";
    case DayCount::ThirtyE360:
        return "30e/360";
    case DayCount::ActActISDA:
        return "act/act-isda";
    case DayCount::ActActICMA:
        return "act/act";
    }
    return "";
}

int day_count_days(DayCount day_count, Date start, Date end) {
    switch (day_count) {
    case DayCount::Thirty360US: {
        int d1 = start.day();
        int d2 = end.day();
        if (is_last_day_of_february(start)) {
            if (is_last_day_of_february(end)) {
                d2 = 30;
            }
            d1 = 30;
        }
        if (d2 == 31 && d1 >= 30) {
            d2 = 30;
        }
        if (d1 == 31) {
            d1 = 30;
        }
        return thirty_360_days(start.year(), start.month(), d1, end.year(), end.month(), d2);
    }
    case DayCount::ThirtyE360: {
        const int d1 = std::min(start.day(), 30);
        const int d2 = std::min(end.day(), 30);
        return thirty_360_days(start.year(), start.month(), d1, end.year(), end.month(), d2);
    }
    default:
        return end - start;
    }
}

double year_fraction(DayCount day_count, Date start, Date end) {
    if (end < start) {
        return -year_fraction(day_count, end, start);
    }
    switch (day_count) {
    case DayCount::Act360:
        return (end - start) / 360.0;
    case DayCount::Act365F:
        return (end - start) / 365.0;
    case DayCount::Thirty360US:
    case DayCount::ThirtyE360:
        return day_count_days(day_count, start, end) / 360.0;
    case DayCount::ActActISDA: {
        const int y1 = start.year();
        const int y2 = end.year();
        const auto basis = [](int y) { return Date::is_leap_year(y) ? 366.0 : 365.0; };
        if (y1 == y2) {
            return (end - start) / basis(y1);
        }
        const double head = (Date(y1 + 1, 1, 1) - start) / basis(y1);
        const double tail = (end - Date(y2, 1, 1)) / basis(y2);
        return head + tail + (y2 - y1 - 1);
    }
    case DayCount::ActActICMA:
        throw Error("act/act (ICMA) year fractions require a coupon reference period");
    }
    return 0.0;
}

double year_fraction(DayCount day_count, Date start, Date end, Date ref_start, Date ref_end,
                     int frequency) {
    if (day_count != DayCount::ActActICMA) {
        return year_fraction(day_count, start, end);
    }
    require(frequency > 0, "act/act (ICMA) requires a positive coupon frequency");
    require(ref_start < ref_end, "act/act (ICMA) reference period is empty");
    require(start >= ref_start && end <= ref_end,
            "act/act (ICMA): period " + start.to_string() + " to " + end.to_string() +
                " is not inside its reference period " + ref_start.to_string() + " to " +
                ref_end.to_string());
    return static_cast<double>(end - start) /
           (frequency * static_cast<double>(ref_end - ref_start));
}

} // namespace fit

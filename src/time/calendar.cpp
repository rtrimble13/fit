#include "fit/time/calendar.hpp"

#include "fit/core/error.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace fit {

namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool is_weekend(Date date) {
    const Weekday w = date.weekday();
    return w == Weekday::Saturday || w == Weekday::Sunday;
}

/// True for a fixed-date holiday observed on the nearest weekday (Sat -> Fri, Sun -> Mon).
bool is_observed(Date date, int month, int day) {
    const int m = date.month();
    const int d = date.day();
    const Weekday w = date.weekday();
    if (m == month && d == day) {
        return true;
    }
    if (w == Weekday::Monday && (date - 1).month() == month && (date - 1).day() == day) {
        return true;
    }
    return w == Weekday::Friday && (date + 1).month() == month && (date + 1).day() == day;
}

bool is_nth_weekday(Date date, int month, Weekday weekday, int n) {
    return date.month() == month && date.weekday() == weekday && (date.day() - 1) / 7 + 1 == n;
}

bool is_last_weekday(Date date, int month, Weekday weekday) {
    return date.month() == month && date.weekday() == weekday &&
           date.day() + 7 > Date::days_in_month(date.year(), month);
}

bool is_us_government_bond_holiday(Date date) {
    const int y = date.year();
    const int m = date.month();
    const int d = date.day();
    const Weekday w = date.weekday();

    // New Year's Day (moved to Monday when it falls on a Sunday).
    if (m == 1 && (d == 1 || (d == 2 && w == Weekday::Monday))) {
        return true;
    }
    if (y >= 1983 && is_nth_weekday(date, 1, Weekday::Monday, 3)) { // Martin Luther King Jr.
        return true;
    }
    if (is_nth_weekday(date, 2, Weekday::Monday, 3)) { // Washington's Birthday
        return true;
    }
    // Good Friday. Since 1996, when it coincides with the US employment report (first
    // Friday of the month) the market has only an early close, so it is a business day.
    if (date == easter_sunday(y) - 2 && (y < 1996 || d > 7)) {
        return true;
    }
    if (is_last_weekday(date, 5, Weekday::Monday)) { // Memorial Day
        return true;
    }
    if (y >= 2022 && is_observed(date, 6, 19)) { // Juneteenth
        return true;
    }
    if (is_observed(date, 7, 4)) { // Independence Day
        return true;
    }
    if (is_nth_weekday(date, 9, Weekday::Monday, 1)) { // Labor Day
        return true;
    }
    if (is_nth_weekday(date, 10, Weekday::Monday, 2)) { // Columbus Day
        return true;
    }
    // Veterans Day: observed on Monday when it falls on a Sunday, but the bond market
    // stays open on the Friday before a Saturday holiday.
    if (m == 11 && (d == 11 || (d == 12 && w == Weekday::Monday))) {
        return true;
    }
    if (is_nth_weekday(date, 11, Weekday::Thursday, 4)) { // Thanksgiving
        return true;
    }
    if (is_observed(date, 12, 25)) { // Christmas
        return true;
    }
    // One-off market closures.
    return date == Date(2004, 6, 11) ||  // President Reagan's funeral
           date == Date(2012, 10, 30) || // Hurricane Sandy
           date == Date(2018, 12, 5);    // President G.H.W. Bush's funeral
}

} // namespace

BusinessDayConvention parse_business_day_convention(std::string_view text) {
    const std::string t = lower(text);
    if (t == "unadjusted" || t == "none") {
        return BusinessDayConvention::Unadjusted;
    }
    if (t == "following" || t == "f") {
        return BusinessDayConvention::Following;
    }
    if (t == "modified-following" || t == "modifiedfollowing" || t == "mf") {
        return BusinessDayConvention::ModifiedFollowing;
    }
    if (t == "preceding" || t == "p") {
        return BusinessDayConvention::Preceding;
    }
    throw Error("unknown business-day convention '" + std::string(text) +
                "' (expected unadjusted, following, modified-following or preceding)");
}

std::string to_string(BusinessDayConvention convention) {
    switch (convention) {
    case BusinessDayConvention::Unadjusted:
        return "unadjusted";
    case BusinessDayConvention::Following:
        return "following";
    case BusinessDayConvention::ModifiedFollowing:
        return "modified-following";
    case BusinessDayConvention::Preceding:
        return "preceding";
    }
    return "";
}

Date easter_sunday(int year) {
    // Anonymous Gregorian algorithm (Meeus/Jones/Butcher).
    const int a = year % 19;
    const int b = year / 100;
    const int c = year % 100;
    const int d = b / 4;
    const int e = b % 4;
    const int f = (b + 8) / 25;
    const int g = (b - f + 1) / 3;
    const int h = (19 * a + b - d - g + 15) % 30;
    const int i = c / 4;
    const int k = c % 4;
    const int l = (32 + 2 * e + 2 * i - h - k) % 7;
    const int m = (a + 11 * h + 22 * l) / 451;
    const int month = (h + l - 7 * m + 114) / 31;
    const int day = (h + l - 7 * m + 114) % 31 + 1;
    return Date(year, month, day);
}

Calendar Calendar::from_name(std::string_view name) {
    const std::string n = lower(name);
    if (n == "none") {
        return Calendar(Rules::None);
    }
    if (n == "weekends" || n == "weekends-only") {
        return Calendar(Rules::WeekendsOnly);
    }
    if (n == "us" || n == "us-govt" || n == "usgovt") {
        return Calendar(Rules::UsGovernmentBond);
    }
    throw Error("unknown calendar '" + std::string(name) + "' (expected none, weekends or us)");
}

std::string Calendar::name() const {
    switch (rules_) {
    case Rules::None:
        return "none";
    case Rules::WeekendsOnly:
        return "weekends";
    case Rules::UsGovernmentBond:
        return "us";
    }
    return "";
}

void Calendar::load_holidays(const std::string& path) {
    std::ifstream in(path);
    require(in.good(), "cannot open holiday file '" + path + "'");
    std::string line;
    int line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        const auto hash = line.find('#');
        if (hash != std::string::npos) {
            line.erase(hash);
        }
        line.erase(std::remove_if(line.begin(), line.end(),
                                  [](unsigned char c) { return std::isspace(c) != 0; }),
                   line.end());
        if (line.empty()) {
            continue;
        }
        try {
            add_holiday(Date::parse(line));
        } catch (const Error& e) {
            throw Error(path + ":" + std::to_string(line_no) + ": " + e.what());
        }
    }
}

bool Calendar::is_business_day(Date date) const {
    if (extra_holidays_.contains(date)) {
        return false;
    }
    switch (rules_) {
    case Rules::None:
        return true;
    case Rules::WeekendsOnly:
        return !is_weekend(date);
    case Rules::UsGovernmentBond:
        return !is_weekend(date) && !is_us_government_bond_holiday(date);
    }
    return true;
}

Date Calendar::adjust(Date date, BusinessDayConvention convention) const {
    if (convention == BusinessDayConvention::Unadjusted) {
        return date;
    }
    Date d = date;
    if (convention == BusinessDayConvention::Preceding) {
        while (!is_business_day(d)) {
            d = d - 1;
        }
        return d;
    }
    while (!is_business_day(d)) {
        d = d + 1;
    }
    if (convention == BusinessDayConvention::ModifiedFollowing && d.month() != date.month()) {
        d = date;
        while (!is_business_day(d)) {
            d = d - 1;
        }
    }
    return d;
}

Date Calendar::advance(Date date, int business_days) const {
    Date d = date;
    const int step = business_days >= 0 ? 1 : -1;
    for (int remaining = business_days >= 0 ? business_days : -business_days; remaining > 0;) {
        d = d + step;
        if (is_business_day(d)) {
            --remaining;
        }
    }
    return d;
}

} // namespace fit

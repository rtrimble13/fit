#include "fit/time/date.hpp"

#include "fit/core/error.hpp"

#include <cctype>
#include <charconv>
#include <cstdio>

namespace fit {

namespace {

constexpr int kMinYear = 1900;
constexpr int kMaxYear = 2299;

// Civil-date algorithms after Howard Hinnant, "chrono-Compatible Low-Level Date Algorithms".
int days_from_civil(int y, int m, int d) {
    y -= m <= 2 ? 1 : 0;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

struct Civil {
    int year;
    int month;
    int day;
};

Civil civil_from_days(int z) {
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const int doe = z - era * 146097;
    const int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int y = yoe + era * 400;
    const int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int mp = (5 * doy + 2) / 153;
    const int d = doy - (153 * mp + 2) / 5 + 1;
    const int m = mp + (mp < 10 ? 3 : -9);
    return {y + (m <= 2 ? 1 : 0), m, d};
}

bool parse_int(std::string_view text, int& out) {
    if (text.empty()) {
        return false;
    }
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, out);
    return result.ec == std::errc{} && result.ptr == end;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Period

bool Period::looks_like(std::string_view text) {
    if (text.size() < 2) {
        return false;
    }
    const char unit = static_cast<char>(std::toupper(static_cast<unsigned char>(text.back())));
    if (unit != 'D' && unit != 'W' && unit != 'M' && unit != 'Y') {
        return false;
    }
    int n = 0;
    return parse_int(text.substr(0, text.size() - 1), n) && n >= 0;
}

Period Period::parse(std::string_view text) {
    require(looks_like(text),
            "invalid tenor '" + std::string(text) + "' (expected e.g. 1W, 3M, 10Y)");
    Period p;
    parse_int(text.substr(0, text.size() - 1), p.length);
    switch (std::toupper(static_cast<unsigned char>(text.back()))) {
    case 'D':
        p.unit = TimeUnit::Days;
        break;
    case 'W':
        p.unit = TimeUnit::Weeks;
        break;
    case 'M':
        p.unit = TimeUnit::Months;
        break;
    default:
        p.unit = TimeUnit::Years;
        break;
    }
    return p;
}

std::string Period::to_string() const {
    const char* suffix = "D";
    switch (unit) {
    case TimeUnit::Days:
        suffix = "D";
        break;
    case TimeUnit::Weeks:
        suffix = "W";
        break;
    case TimeUnit::Months:
        suffix = "M";
        break;
    case TimeUnit::Years:
        suffix = "Y";
        break;
    }
    return std::to_string(length) + suffix;
}

double Period::approx_years() const {
    switch (unit) {
    case TimeUnit::Days:
        return length / 365.0;
    case TimeUnit::Weeks:
        return length * 7.0 / 365.0;
    case TimeUnit::Months:
        return length / 12.0;
    case TimeUnit::Years:
        return length;
    }
    return 0.0;
}

// ---------------------------------------------------------------------------------------------
// Date

bool Date::is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int Date::days_in_month(int year, int month) {
    static constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    require(month >= 1 && month <= 12, "invalid month " + std::to_string(month));
    return month == 2 && is_leap_year(year) ? 29 : kDays[month - 1];
}

Date::Date(int year, int month, int day) {
    require(year >= kMinYear && year <= kMaxYear,
            "year " + std::to_string(year) + " outside supported range 1900-2299");
    require(month >= 1 && month <= 12, "invalid month " + std::to_string(month));
    require(day >= 1 && day <= days_in_month(year, month), "invalid day " + std::to_string(day) +
                                                               " for " + std::to_string(year) +
                                                               "-" + std::to_string(month));
    serial_ = days_from_civil(year, month, day);
}

Date Date::from_serial(int serial) {
    const Civil c = civil_from_days(serial);
    return Date(c.year, c.month, c.day); // re-validates the supported range
}

bool Date::looks_like(std::string_view text) {
    return text.size() == 10 && text[4] == '-' && text[7] == '-' &&
           std::isdigit(static_cast<unsigned char>(text[0])) != 0;
}

Date Date::parse(std::string_view text) {
    int y = 0;
    int m = 0;
    int d = 0;
    const bool ok = looks_like(text) && parse_int(text.substr(0, 4), y) &&
                    parse_int(text.substr(5, 2), m) && parse_int(text.substr(8, 2), d);
    require(ok, "invalid date '" + std::string(text) + "' (expected YYYY-MM-DD)");
    try {
        return Date(y, m, d);
    } catch (const Error& e) {
        throw Error("invalid date '" + std::string(text) + "': " + e.what());
    }
}

int Date::year() const {
    return civil_from_days(serial_).year;
}
int Date::month() const {
    return civil_from_days(serial_).month;
}
int Date::day() const {
    return civil_from_days(serial_).day;
}

Weekday Date::weekday() const {
    // 1970-01-01 was a Thursday.
    const int w = ((serial_ % 7) + 7 + 3) % 7;
    return static_cast<Weekday>(w);
}

std::string Date::to_string() const {
    const Civil c = civil_from_days(serial_);
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", c.year, c.month, c.day);
    return buffer;
}

bool Date::is_end_of_month() const {
    const Civil c = civil_from_days(serial_);
    return c.day == days_in_month(c.year, c.month);
}

Date Date::end_of_month() const {
    const Civil c = civil_from_days(serial_);
    return Date(c.year, c.month, days_in_month(c.year, c.month));
}

Date Date::add_months(int months, bool end_of_month) const {
    const Civil c = civil_from_days(serial_);
    const int total = c.year * 12 + (c.month - 1) + months;
    const int y = total / 12;
    const int m = total % 12 + 1;
    const int last = days_in_month(y, m);
    const bool snap = end_of_month && c.day == days_in_month(c.year, c.month);
    return Date(y, m, snap ? last : (c.day < last ? c.day : last));
}

Date Date::add(Period period, bool end_of_month) const {
    switch (period.unit) {
    case TimeUnit::Days:
        return *this + period.length;
    case TimeUnit::Weeks:
        return *this + 7 * period.length;
    case TimeUnit::Months:
        return add_months(period.length, end_of_month);
    case TimeUnit::Years:
        return add_months(12 * period.length, end_of_month);
    }
    return *this;
}

} // namespace fit

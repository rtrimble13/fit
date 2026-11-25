#pragma once

#include <compare>
#include <string>
#include <string_view>

namespace fit {

enum class Weekday { Monday = 0, Tuesday, Wednesday, Thursday, Friday, Saturday, Sunday };

enum class TimeUnit { Days, Weeks, Months, Years };

/// A length of time expressed in calendar units, e.g. `3M` or `10Y`.
struct Period {
    int length = 0;
    TimeUnit unit = TimeUnit::Days;

    /// Parses `<integer><D|W|M|Y>` (case-insensitive), e.g. "1W", "18M", "30Y".
    static Period parse(std::string_view text);
    /// Returns true when `text` has the shape of a tenor.
    static bool looks_like(std::string_view text);
    std::string to_string() const;
    /// Approximate length in years; used only for ordering and labelling.
    double approx_years() const;
};

/// A proleptic Gregorian calendar date with day resolution.
///
/// Stored as a serial day number (days since 1970-01-01), so differences and comparisons are
/// trivial. Valid range is 1900-01-01 to 2299-12-31.
class Date {
public:
    Date() = default;
    Date(int year, int month, int day);

    /// Parses ISO-8601 `YYYY-MM-DD`.
    static Date parse(std::string_view text);
    static bool looks_like(std::string_view text);
    static Date from_serial(int serial);

    int serial() const { return serial_; }
    int year() const;
    int month() const;
    int day() const;
    Weekday weekday() const;
    std::string to_string() const;

    bool is_end_of_month() const;
    Date end_of_month() const;

    /// Adds calendar months, clamping the day to the end of the target month. When
    /// `end_of_month` is true and this date is a month end, the result is a month end too.
    Date add_months(int months, bool end_of_month = false) const;
    /// Adds a period without any business-day adjustment.
    Date add(Period period, bool end_of_month = false) const;

    Date operator+(int days) const { return from_serial(serial_ + days); }
    Date operator-(int days) const { return from_serial(serial_ - days); }
    int operator-(const Date& other) const { return serial_ - other.serial_; }
    auto operator<=>(const Date&) const = default;

    static bool is_leap_year(int year);
    static int days_in_month(int year, int month);

private:
    int serial_ = 0;
};

/// The third Wednesday of a month: the IMM date on which quarterly interest-rate futures
/// reference periods start and end.
Date third_wednesday(int year, int month);

} // namespace fit

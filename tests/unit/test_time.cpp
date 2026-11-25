#include "fit/core/error.hpp"
#include "fit/time/calendar.hpp"
#include "fit/time/date.hpp"
#include "fit/time/daycount.hpp"
#include "fit/time/schedule.hpp"

#include <doctest/doctest.h>

using namespace fit;

TEST_CASE("date: construction, parsing and round trips") {
    const Date d(2026, 11, 17);
    CHECK(d.year() == 2026);
    CHECK(d.month() == 11);
    CHECK(d.day() == 17);
    CHECK(d.weekday() == Weekday::Tuesday);
    CHECK(d.to_string() == "2026-11-17");
    CHECK(Date::parse("2026-11-17") == d);
    CHECK(Date(1970, 1, 1).serial() == 0);
    CHECK(Date(2000, 3, 1) - Date(2000, 2, 28) == 2); // leap year
    CHECK(Date(2100, 3, 1) - Date(2100, 2, 28) == 1); // century non-leap year

    // Every serial in the supported range maps to a unique civil date and back.
    for (Date x(1999, 12, 25); x < Date(2004, 3, 5); x = x + 1) {
        CHECK(Date(x.year(), x.month(), x.day()) == x);
    }
}

TEST_CASE("date: invalid input is rejected with a clear error") {
    CHECK_THROWS_AS(Date(2025, 2, 29), Error);
    CHECK_THROWS_AS(Date(2025, 13, 1), Error);
    CHECK_THROWS_AS(Date::parse("17/11/2026"), Error);
    CHECK_THROWS_AS(Date::parse("2026-11-31"), Error);
    CHECK_THROWS_WITH(Date::parse("garbage"), "invalid date 'garbage' (expected YYYY-MM-DD)");
}

TEST_CASE("date: month arithmetic clamps and honours end-of-month") {
    CHECK(Date(2026, 1, 31).add_months(1) == Date(2026, 2, 28));
    CHECK(Date(2024, 1, 31).add_months(1) == Date(2024, 2, 29));
    CHECK(Date(2026, 2, 28).add_months(6) == Date(2026, 8, 28));
    CHECK(Date(2026, 2, 28).add_months(6, true) == Date(2026, 8, 31));
    CHECK(Date(2026, 11, 30).add_months(-12, true) == Date(2025, 11, 30));
    CHECK(Date(2026, 3, 15).add_months(-3) == Date(2025, 12, 15));
    CHECK(Date(2026, 11, 17).add(Period::parse("10Y")) == Date(2036, 11, 17));
    CHECK(Date(2026, 11, 17).add(Period::parse("2w")) == Date(2026, 12, 1));
}

TEST_CASE("date: third Wednesday (IMM dates)") {
    // Hand-checked against a calendar: 1 Dec 2026 is a Tuesday, 1 Mar 2027 a Monday, 1 Jan 2025 a
    // Wednesday (so its third Wednesday is the 15th), and 19 Jun 2024 was both the third
    // Wednesday and Juneteenth.
    CHECK(third_wednesday(2026, 12) == Date(2026, 12, 16));
    CHECK(third_wednesday(2027, 3) == Date(2027, 3, 17));
    CHECK(third_wednesday(2027, 6) == Date(2027, 6, 16));
    CHECK(third_wednesday(2027, 9) == Date(2027, 9, 15));
    CHECK(third_wednesday(2025, 1) == Date(2025, 1, 15));
    CHECK(third_wednesday(2024, 6) == Date(2024, 6, 19));
    for (int month = 1; month <= 12; ++month) {
        const Date d = third_wednesday(2031, month);
        CHECK(d.weekday() == Weekday::Wednesday);
        CHECK(d.day() >= 15);
        CHECK(d.day() <= 21);
    }
}

TEST_CASE("period: parsing") {
    CHECK(Period::parse("18M").length == 18);
    CHECK(Period::parse("18M").unit == TimeUnit::Months);
    CHECK(Period::parse("3m").to_string() == "3M");
    CHECK(Period::parse("6M").approx_years() == doctest::Approx(0.5));
    CHECK_THROWS_AS(Period::parse("M"), Error);
    CHECK_THROWS_AS(Period::parse("5X"), Error);
    CHECK_THROWS_AS(Period::parse("1.5Y"), Error);
}

TEST_CASE("calendar: US government bond holidays (cross-checked against QuantLib)") {
    const Calendar us(Calendar::Rules::UsGovernmentBond);
    CHECK(us.is_holiday(Date(2026, 1, 1)));   // New Year's Day
    CHECK(us.is_holiday(Date(2026, 1, 19)));  // Martin Luther King Jr. Day
    CHECK(us.is_holiday(Date(2026, 2, 16)));  // Washington's Birthday
    CHECK(us.is_holiday(Date(2025, 4, 18)));  // Good Friday
    CHECK(us.is_holiday(Date(2026, 5, 25)));  // Memorial Day
    CHECK(us.is_holiday(Date(2026, 6, 19)));  // Juneteenth
    CHECK(us.is_holiday(Date(2026, 7, 3)));   // Independence Day observed (4th is a Saturday)
    CHECK(us.is_holiday(Date(2026, 9, 7)));   // Labor Day
    CHECK(us.is_holiday(Date(2026, 10, 12))); // Columbus Day
    CHECK(us.is_holiday(Date(2026, 11, 11))); // Veterans Day
    CHECK(us.is_holiday(Date(2026, 11, 26))); // Thanksgiving
    CHECK(us.is_holiday(Date(2026, 12, 25))); // Christmas
    CHECK(us.is_holiday(Date(2022, 6, 20)));  // Juneteenth observed on Monday
    CHECK(us.is_holiday(Date(2012, 10, 30))); // Hurricane Sandy closure

    CHECK(us.is_business_day(Date(2026, 4, 3)));   // Good Friday on payrolls day: open
    CHECK(us.is_business_day(Date(2023, 11, 10))); // Friday before a Saturday Veterans Day
    CHECK(us.is_business_day(Date(2021, 6, 18)));  // Juneteenth not yet a market holiday
    CHECK(us.is_business_day(Date(2026, 11, 17)));
    CHECK(easter_sunday(2026) == Date(2026, 4, 5));
}

TEST_CASE("calendar: adjustment conventions and business-day advance") {
    const Calendar weekends(Calendar::Rules::WeekendsOnly);
    const Date saturday(2026, 10, 31);
    CHECK(weekends.adjust(saturday, BusinessDayConvention::Following) == Date(2026, 11, 2));
    CHECK(weekends.adjust(saturday, BusinessDayConvention::ModifiedFollowing) ==
          Date(2026, 10, 30));
    CHECK(weekends.adjust(saturday, BusinessDayConvention::Preceding) == Date(2026, 10, 30));
    CHECK(weekends.adjust(saturday, BusinessDayConvention::Unadjusted) == saturday);
    CHECK(weekends.advance(Date(2026, 11, 20), 1) == Date(2026, 11, 23));
    CHECK(weekends.advance(Date(2026, 11, 23), -1) == Date(2026, 11, 20));
    CHECK(weekends.advance(Date(2026, 11, 17), 0) == Date(2026, 11, 17));

    Calendar custom(Calendar::Rules::WeekendsOnly);
    custom.add_holiday(Date(2026, 11, 18));
    CHECK(custom.advance(Date(2026, 11, 17), 1) == Date(2026, 11, 19));
    CHECK(Calendar::from_name("US").name() == "us");
    CHECK_THROWS_AS(Calendar::from_name("mars"), Error);
}

TEST_CASE("day count: year fractions") {
    // ISDA 1999 "EMU and market conventions" example: 1 Nov 2003 to 1 May 2004.
    CHECK(year_fraction(DayCount::ActActISDA, Date(2003, 11, 1), Date(2004, 5, 1)) ==
          doctest::Approx(61.0 / 365.0 + 121.0 / 366.0).epsilon(1e-14));
    CHECK(year_fraction(DayCount::ActActICMA, Date(2003, 11, 1), Date(2004, 5, 1),
                        Date(2003, 11, 1), Date(2004, 5, 1), 2) == doctest::Approx(0.5));
    // ISDA example, short first period: 1 Feb 1999 to 1 Jul 1999, annual coupons.
    CHECK(year_fraction(DayCount::ActActICMA, Date(1999, 2, 1), Date(1999, 7, 1), Date(1998, 7, 1),
                        Date(1999, 7, 1), 1) == doctest::Approx(150.0 / 365.0).epsilon(1e-14));
    CHECK(year_fraction(DayCount::ActActISDA, Date(1999, 2, 1), Date(1999, 7, 1)) ==
          doctest::Approx(150.0 / 365.0).epsilon(1e-14));

    CHECK(year_fraction(DayCount::Act360, Date(2026, 1, 1), Date(2026, 7, 1)) ==
          doctest::Approx(181.0 / 360.0));
    CHECK(year_fraction(DayCount::Act365F, Date(2024, 1, 1), Date(2025, 1, 1)) ==
          doctest::Approx(366.0 / 365.0));
    CHECK(year_fraction(DayCount::Act365F, Date(2025, 1, 1), Date(2024, 1, 1)) ==
          doctest::Approx(-366.0 / 365.0));
    CHECK_THROWS_AS(year_fraction(DayCount::ActActICMA, Date(2026, 1, 1), Date(2026, 7, 1)), Error);
}

TEST_CASE("day count: 30/360 day counting rules") {
    CHECK(day_count_days(DayCount::Thirty360US, Date(2007, 1, 31), Date(2007, 2, 28)) == 28);
    CHECK(day_count_days(DayCount::ThirtyE360, Date(2007, 1, 31), Date(2007, 2, 28)) == 28);
    // End-of-February start date counts as the 30th under the US (SIA) rule only.
    CHECK(day_count_days(DayCount::Thirty360US, Date(2007, 2, 28), Date(2007, 3, 31)) == 30);
    CHECK(day_count_days(DayCount::ThirtyE360, Date(2007, 2, 28), Date(2007, 3, 31)) == 32);
    // A 31st end date rolls back only when the start is the 30th or 31st.
    CHECK(day_count_days(DayCount::Thirty360US, Date(2026, 1, 15), Date(2026, 1, 31)) == 16);
    CHECK(day_count_days(DayCount::ThirtyE360, Date(2026, 1, 15), Date(2026, 1, 31)) == 15);
    CHECK(day_count_days(DayCount::Thirty360US, Date(2026, 1, 30), Date(2026, 7, 31)) == 180);
}

TEST_CASE("day count: parsing accepts market spellings") {
    CHECK(parse_day_count("ACT/360") == DayCount::Act360);
    CHECK(parse_day_count("act/act") == DayCount::ActActICMA);
    CHECK(parse_day_count("Act/Act-ISDA") == DayCount::ActActISDA);
    CHECK(parse_day_count("30/360") == DayCount::Thirty360US);
    CHECK(parse_day_count(to_string(DayCount::ThirtyE360)) == DayCount::ThirtyE360);
    CHECK_THROWS_AS(parse_day_count("act/364"), Error);
}

TEST_CASE("schedule: regular semi-annual schedule") {
    ScheduleSpec spec;
    spec.start = Date(2024, 11, 15);
    spec.maturity = Date(2026, 11, 15);
    spec.frequency = 2;
    const auto periods = make_schedule(spec);
    REQUIRE(periods.size() == 4);
    CHECK(periods.front().accrual_start == Date(2024, 11, 15));
    CHECK(periods.front().accrual_end == Date(2025, 5, 15));
    CHECK(periods.front().is_regular());
    CHECK(periods.back().accrual_end == Date(2026, 11, 15)); // a Sunday
    CHECK(periods.back().payment == Date(2026, 11, 16));     // paid on the Monday
}

TEST_CASE("schedule: short first stub and end-of-month roll") {
    ScheduleSpec spec;
    spec.start = Date(2026, 1, 10);
    spec.maturity = Date(2027, 8, 31);
    spec.frequency = 2;
    const auto periods = make_schedule(spec);
    REQUIRE(periods.size() == 4);
    CHECK_FALSE(periods[0].is_regular());
    CHECK(periods[0].accrual_start == Date(2026, 1, 10));
    CHECK(periods[0].ref_start == Date(2025, 8, 31));
    CHECK(periods[0].accrual_end == Date(2026, 2, 28)); // month-end roll
    CHECK(periods[1].accrual_end == Date(2026, 8, 31));
    CHECK(periods[2].accrual_end == Date(2027, 2, 28));

    spec.end_of_month = false;
    spec.maturity = Date(2027, 8, 28);
    CHECK(make_schedule(spec)[1].accrual_end == Date(2026, 8, 28));

    spec.frequency = 3;
    CHECK_THROWS_AS(make_schedule(spec), Error);
    spec.frequency = 2;
    spec.start = spec.maturity;
    CHECK_THROWS_AS(make_schedule(spec), Error);
}

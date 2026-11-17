#pragma once

#include "fit/time/calendar.hpp"
#include "fit/time/date.hpp"
#include "fit/time/daycount.hpp"
#include "fit/time/schedule.hpp"

#include <optional>
#include <string>
#include <vector>

namespace fit {

/// Contract terms of a fixed-rate bullet bond. A zero-coupon bond has `coupon == 0`.
struct BondSpec {
    std::string id;
    std::optional<Date> issue; ///< dated date; when absent a regular schedule is assumed
    Date maturity;
    double coupon = 0.0; ///< annual coupon rate, decimal
    int frequency = 2;   ///< coupons per year; also the yield compounding frequency
    DayCount day_count = DayCount::ActActICMA;
    Calendar calendar;
    BusinessDayConvention convention = BusinessDayConvention::Following;
    bool end_of_month = true;
    int settle_days = 1;       ///< business days from trade date to settlement
    double redemption = 100.0; ///< redemption value per 100 face
};

/// A dated payment per 100 face.
struct Cashflow {
    Date payment; ///< business-day adjusted payment date
    double coupon = 0.0;
    double principal = 0.0;
    Date accrual_start; ///< unadjusted
    Date accrual_end;   ///< unadjusted

    double amount() const { return coupon + principal; }
};

/// A fixed-rate bullet bond: contract terms plus the generated cashflow schedule.
/// All amounts are per 100 face.
class Bond {
public:
    explicit Bond(BondSpec spec);

    const BondSpec& spec() const { return spec_; }
    const std::vector<SchedulePeriod>& periods() const { return periods_; }
    /// All cashflows, one per coupon period, the last including the redemption.
    const std::vector<Cashflow>& cashflows() const { return cashflows_; }
    Date maturity() const { return spec_.maturity; }

    /// Standard settlement date for a trade on `trade_date`.
    Date settlement_date(Date trade_date) const;

    /// Accrued interest at `settle`.
    double accrued(Date settle) const;

    /// Index of the first cashflow paid strictly after `settle`; throws when the bond has
    /// matured.
    std::size_t first_cashflow_after(Date settle) const;

private:
    BondSpec spec_;
    std::vector<SchedulePeriod> periods_;
    std::vector<Cashflow> cashflows_;
};

} // namespace fit

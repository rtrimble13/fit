#pragma once

#include "fit/math/interpolation.hpp"
#include "fit/time/date.hpp"
#include "fit/time/daycount.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fit {

enum class InterpolationMethod {
    LinearZero,    ///< linear in continuously compounded zero rates
    FlatForward,   ///< linear in -ln(DF): piecewise-constant instantaneous forwards
    CubicZero,     ///< natural cubic spline in zero rates
    MonotoneCubic, ///< monotone cubic (PCHIP) in -ln(DF)
    MonotoneConvex ///< Hagan-West monotone convex in -ln(DF): continuous, positive forwards
};

InterpolationMethod parse_interpolation_method(std::string_view text);
std::string to_string(InterpolationMethod method);

/// A piecewise-linear zero-rate spread as a function of time, flat outside its nodes.
/// A single node describes a parallel shift. Values are decimals (0.0001 == 1bp).
class ZeroShift {
public:
    ZeroShift() = default;
    static ZeroShift parallel(double spread);
    static ZeroShift piecewise(std::vector<double> times, std::vector<double> spreads);
    /// Triangular bump of `size` at `times[index]`, falling to zero at the neighbouring key
    /// times and flat beyond the first/last key. Bumps over all indices sum to a parallel
    /// shift of `size`.
    static ZeroShift key_rate(const std::vector<double>& times, std::size_t index, double size);

    double at(double t) const;
    bool is_zero() const;
    ZeroShift scaled(double factor) const;

private:
    std::vector<double> times_;
    std::vector<double> spreads_;
};

/// A discount curve anchored at an as-of date.
///
/// The time axis is ACT/365F from the as-of date and rates quoted by the curve are
/// continuously compounded unless stated otherwise (ADR-0007).
class YieldCurve {
public:
    virtual ~YieldCurve() = default;

    Date as_of() const { return as_of_; }
    double time(Date date) const { return (date - as_of_) / 365.0; }

    /// Discount factor from the as-of date to time `t >= 0`.
    virtual double discount_t(double t) const = 0;

    double discount(Date date) const;
    /// Continuously compounded zero rate to `t` (the limit at `t == 0` is the short rate).
    double zero_rate_t(double t) const;
    double zero_rate(Date date) const { return zero_rate_t(time(date)); }
    /// Zero rate compounded `frequency` times a year (ACT/365F time).
    double zero_rate_compounded(Date date, int frequency) const;
    /// Continuously compounded forward rate between two dates.
    double forward_rate(Date start, Date end) const;
    /// Simply compounded forward rate over `[start, end]` under `day_count`.
    double simple_forward_rate(Date start, Date end, DayCount day_count) const;
    /// Instantaneous forward rate at `t`.
    double instantaneous_forward_t(double t) const;

protected:
    explicit YieldCurve(Date as_of) : as_of_(as_of) {}

private:
    Date as_of_;
};

using CurvePtr = std::shared_ptr<const YieldCurve>;

/// A zero curve interpolated through pillar nodes.
class InterpolatedCurve final : public YieldCurve {
public:
    /// `times` are strictly increasing positive ACT/365F year fractions; `zero_rates` are
    /// continuously compounded.
    InterpolatedCurve(Date as_of, std::vector<double> times, std::vector<double> zero_rates,
                      InterpolationMethod method);

    double discount_t(double t) const override;

    const std::vector<double>& times() const { return times_; }
    const std::vector<double>& zero_rates() const { return zero_rates_; }
    InterpolationMethod method() const { return method_; }

private:
    std::vector<double> times_;
    std::vector<double> zero_rates_;
    InterpolationMethod method_;
    std::optional<Interpolation> interpolation_;
    std::optional<MonotoneConvex> monotone_convex_;
};

/// `base` with a zero-rate shift added: DF'(t) = DF(t) * exp(-shift(t) * t).
class ShiftedCurve final : public YieldCurve {
public:
    ShiftedCurve(CurvePtr base, ZeroShift shift);
    double discount_t(double t) const override;

private:
    CurvePtr base_;
    ZeroShift shift_;
};

/// The curve implied at a future date if today's forwards are realised:
/// DF_h(t) = DF(t_h + t) / DF(t_h).
class ForwardImpliedCurve final : public YieldCurve {
public:
    ForwardImpliedCurve(CurvePtr base, Date horizon);
    double discount_t(double t) const override;

private:
    CurvePtr base_;
    double horizon_time_;
};

/// `base` re-anchored at another date with its zero rates unchanged tenor-by-tenor
/// (the "unchanged curve" assumption of roll-down analysis).
class RebasedCurve final : public YieldCurve {
public:
    RebasedCurve(CurvePtr base, Date as_of);
    double discount_t(double t) const override;

private:
    CurvePtr base_;
};

/// Applies `shift` to `curve`; returns `curve` itself when the shift is zero.
CurvePtr shifted(CurvePtr curve, const ZeroShift& shift);

} // namespace fit

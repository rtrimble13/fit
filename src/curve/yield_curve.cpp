#include "fit/curve/yield_curve.hpp"

#include "fit/core/error.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace fit {

namespace {

constexpr double kShortTime = 1.0 / 365.0;

/// Schemes that interpolate -ln DF(t) = z(t) t through an anchor at t = 0.
bool interpolates_log_discount(InterpolationMethod method) {
    return method == InterpolationMethod::FlatForward ||
           method == InterpolationMethod::MonotoneCubic ||
           method == InterpolationMethod::MonotoneConvex;
}

} // namespace

InterpolationMethod parse_interpolation_method(std::string_view text) {
    std::string t(text);
    std::transform(t.begin(), t.end(), t.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (t == "linear-zero" || t == "linear") {
        return InterpolationMethod::LinearZero;
    }
    if (t == "flat-forward" || t == "log-linear" || t == "loglinear") {
        return InterpolationMethod::FlatForward;
    }
    if (t == "cubic-zero" || t == "cubic") {
        return InterpolationMethod::CubicZero;
    }
    if (t == "monotone-cubic" || t == "pchip") {
        return InterpolationMethod::MonotoneCubic;
    }
    if (t == "monotone-convex") {
        return InterpolationMethod::MonotoneConvex;
    }
    throw Error("unknown interpolation '" + std::string(text) +
                "' (expected linear-zero, flat-forward, cubic-zero, monotone-cubic or "
                "monotone-convex)");
}

std::string to_string(InterpolationMethod method) {
    switch (method) {
    case InterpolationMethod::LinearZero:
        return "linear-zero";
    case InterpolationMethod::FlatForward:
        return "flat-forward";
    case InterpolationMethod::CubicZero:
        return "cubic-zero";
    case InterpolationMethod::MonotoneCubic:
        return "monotone-cubic";
    case InterpolationMethod::MonotoneConvex:
        return "monotone-convex";
    }
    return "";
}

Extrapolation parse_extrapolation(std::string_view text) {
    std::string t(text);
    std::transform(t.begin(), t.end(), t.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (t == "flat-forward") {
        return Extrapolation::FlatForward;
    }
    if (t == "flat-zero") {
        return Extrapolation::FlatZero;
    }
    throw Error("unknown extrapolation '" + std::string(text) +
                "' (expected flat-forward or flat-zero)");
}

std::string to_string(Extrapolation extrapolation) {
    switch (extrapolation) {
    case Extrapolation::FlatForward:
        return "flat-forward";
    case Extrapolation::FlatZero:
        return "flat-zero";
    }
    return "";
}

Extrapolation natural_extrapolation(InterpolationMethod method) {
    return interpolates_log_discount(method) ? Extrapolation::FlatForward : Extrapolation::FlatZero;
}

// ---------------------------------------------------------------------------------------------
// ZeroShift

ZeroShift ZeroShift::parallel(double spread) {
    ZeroShift s;
    s.times_ = {0.0};
    s.spreads_ = {spread};
    return s;
}

ZeroShift ZeroShift::piecewise(std::vector<double> times, std::vector<double> spreads) {
    require(!times.empty() && times.size() == spreads.size(),
            "zero shift: times and spreads must be non-empty and of equal size");
    for (std::size_t i = 1; i < times.size(); ++i) {
        require(times[i] > times[i - 1], "zero shift: times must be strictly increasing");
    }
    ZeroShift s;
    s.times_ = std::move(times);
    s.spreads_ = std::move(spreads);
    return s;
}

ZeroShift ZeroShift::key_rate(const std::vector<double>& times, std::size_t index, double size) {
    require(index < times.size(), "key-rate shift: index out of range");
    std::vector<double> spreads(times.size(), 0.0);
    spreads[index] = size;
    return piecewise(times, std::move(spreads));
}

double ZeroShift::at(double t) const {
    if (times_.empty()) {
        return 0.0;
    }
    if (t <= times_.front()) {
        return spreads_.front();
    }
    if (t >= times_.back()) {
        return spreads_.back();
    }
    const auto it = std::upper_bound(times_.begin(), times_.end(), t);
    const auto i = static_cast<std::size_t>(it - times_.begin()) - 1;
    const double w = (t - times_[i]) / (times_[i + 1] - times_[i]);
    return spreads_[i] + w * (spreads_[i + 1] - spreads_[i]);
}

bool ZeroShift::is_zero() const {
    return std::all_of(spreads_.begin(), spreads_.end(), [](double s) { return s == 0.0; });
}

ZeroShift ZeroShift::scaled(double factor) const {
    ZeroShift s = *this;
    for (double& v : s.spreads_) {
        v *= factor;
    }
    return s;
}

// ---------------------------------------------------------------------------------------------
// YieldCurve

double YieldCurve::discount(Date date) const {
    require(date >= as_of_,
            "date " + date.to_string() + " precedes the curve as-of date " + as_of_.to_string());
    return discount_t(time(date));
}

double YieldCurve::zero_rate_t(double t) const {
    const double tt = std::max(t, kShortTime);
    return -std::log(discount_t(tt)) / tt;
}

double YieldCurve::zero_rate_compounded(Date date, int frequency) const {
    require(frequency > 0, "compounding frequency must be positive");
    const double z = zero_rate(date);
    return frequency * (std::exp(z / frequency) - 1.0);
}

double YieldCurve::forward_rate(Date start, Date end) const {
    require(end > start, "forward rate: end date must follow start date");
    return std::log(discount(start) / discount(end)) / (time(end) - time(start));
}

double YieldCurve::simple_forward_rate(Date start, Date end, DayCount day_count) const {
    require(end > start, "forward rate: end date must follow start date");
    return (discount(start) / discount(end) - 1.0) / year_fraction(day_count, start, end);
}

double YieldCurve::instantaneous_forward_t(double t) const {
    const double h = 0.5 * kShortTime;
    const double lo = std::max(0.0, t - h);
    const double hi = lo + 2.0 * h;
    return std::log(discount_t(lo) / discount_t(hi)) / (hi - lo);
}

// ---------------------------------------------------------------------------------------------
// InterpolatedCurve

InterpolatedCurve::InterpolatedCurve(Date as_of, std::vector<double> times,
                                     std::vector<double> zero_rates, InterpolationMethod method)
    : InterpolatedCurve(as_of, std::move(times), std::move(zero_rates), method,
                        natural_extrapolation(method)) {}

InterpolatedCurve::InterpolatedCurve(Date as_of, std::vector<double> times,
                                     std::vector<double> zero_rates, InterpolationMethod method,
                                     Extrapolation extrapolation)
    : YieldCurve(as_of),
      times_(std::move(times)),
      zero_rates_(std::move(zero_rates)),
      method_(method),
      extrapolation_(extrapolation) {
    require(!times_.empty(), "curve: at least one node is required");
    require(times_.size() == zero_rates_.size(), "curve: node times and rates differ in size");
    require(times_.front() > 0.0, "curve: node times must be positive");
    for (std::size_t i = 1; i < times_.size(); ++i) {
        require(times_[i] > times_[i - 1], "curve: node times must be strictly increasing");
    }

    if (interpolates_log_discount(method_)) {
        // Interpolate y(t) = z(t) * t = -ln DF(t), anchored at y(0) = 0.
        std::vector<double> x{0.0};
        std::vector<double> y{0.0};
        for (std::size_t i = 0; i < times_.size(); ++i) {
            x.push_back(times_[i]);
            y.push_back(zero_rates_[i] * times_[i]);
        }
        if (method_ == InterpolationMethod::MonotoneConvex) {
            monotone_convex_.emplace(std::move(x), std::move(y));
        } else {
            interpolation_ = method_ == InterpolationMethod::FlatForward
                                 ? Interpolation::linear(std::move(x), std::move(y))
                                 : Interpolation::monotone_cubic(std::move(x), std::move(y));
        }
    } else if (times_.size() >= 2) {
        interpolation_ = method_ == InterpolationMethod::LinearZero
                             ? Interpolation::linear(times_, zero_rates_)
                             : Interpolation::natural_cubic(times_, zero_rates_);
    }

    // The instantaneous forward at the last pillar, approached from inside the curve: the
    // slope of -ln DF there. For the zero-rate schemes that is z + t z'(t).
    // A zero-rate scheme with one pillar has no interpolant and a flat zero curve (slope 0).
    const double t_last = times_.back();
    if (monotone_convex_) {
        last_forward_ = monotone_convex_->derivative(t_last);
    } else if (!interpolates_log_discount(method_)) {
        const double slope = interpolation_ ? interpolation_->derivative(t_last) : 0.0;
        last_forward_ = zero_rates_.back() + t_last * slope;
    } else if (interpolation_) {
        last_forward_ = interpolation_->derivative(t_last);
    }
}

double InterpolatedCurve::discount_t(double t) const {
    require(t >= 0.0, "curve: negative time");
    if (t == 0.0) {
        return 1.0;
    }
    const double t_last = times_.back();
    if (t > t_last && extrapolation_ != natural_extrapolation(method_)) {
        // The scheme's other extrapolation; its natural one is the interpolant's own extension
        // below.
        const double z_last = zero_rates_.back();
        if (extrapolation_ == Extrapolation::FlatZero) {
            return std::exp(-z_last * t);
        }
        return std::exp(-(z_last * t_last + last_forward_ * (t - t_last)));
    }
    if (interpolates_log_discount(method_)) {
        // Beyond the last node the interpolant continues linearly: flat forward extrapolation.
        return std::exp(
            -(monotone_convex_ ? monotone_convex_->value(t) : interpolation_->value(t)));
    }
    // Zero-rate schemes extrapolate the zero rate flat on both sides.
    const double clamped = std::clamp(t, times_.front(), times_.back());
    const double z = interpolation_ ? interpolation_->value(clamped) : zero_rates_.front();
    return std::exp(-z * t);
}

// ---------------------------------------------------------------------------------------------
// Derived curves

ShiftedCurve::ShiftedCurve(CurvePtr base, ZeroShift shift)
    : YieldCurve(base->as_of()), base_(std::move(base)), shift_(std::move(shift)) {}

double ShiftedCurve::discount_t(double t) const {
    return base_->discount_t(t) * std::exp(-shift_.at(t) * t);
}

ForwardImpliedCurve::ForwardImpliedCurve(CurvePtr base, Date horizon)
    : YieldCurve(horizon), base_(std::move(base)), horizon_time_(base_->time(horizon)) {
    require(horizon_time_ >= 0.0,
            "forward curve: horizon " + horizon.to_string() + " precedes the curve as-of date");
}

double ForwardImpliedCurve::discount_t(double t) const {
    return base_->discount_t(horizon_time_ + t) / base_->discount_t(horizon_time_);
}

RebasedCurve::RebasedCurve(CurvePtr base, Date as_of) : YieldCurve(as_of), base_(std::move(base)) {}

double RebasedCurve::discount_t(double t) const {
    return base_->discount_t(t);
}

CurvePtr shifted(CurvePtr curve, const ZeroShift& shift) {
    if (shift.is_zero()) {
        return curve;
    }
    return std::make_shared<ShiftedCurve>(std::move(curve), shift);
}

} // namespace fit

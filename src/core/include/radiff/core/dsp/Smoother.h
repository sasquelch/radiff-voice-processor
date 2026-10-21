#pragma once

#include "radiff/core/dsp/Units.h"

#include <cmath>

namespace radiff::core::dsp {

/// One-pole parameter smoother for gains and mixes (removes zipper noise).
class OnePoleSmoother {
public:
    void prepare(double sampleRate, double timeMs = 10.0) noexcept {
        coef_ = timeConstantCoefficient(timeMs, sampleRate);
    }
    void reset(double value) noexcept { current_ = target_ = value; }
    void setTarget(double v) noexcept { target_ = v; }
    [[nodiscard]] double target() const noexcept { return target_; }
    [[nodiscard]] double current() const noexcept { return current_; }
    [[nodiscard]] bool isSettled() const noexcept { return std::fabs(current_ - target_) < 1e-7; }

    [[nodiscard]] inline double next() noexcept {
        current_ = target_ + coef_ * (current_ - target_);
        if (std::fabs(current_ - target_) < 1e-9)
            current_ = target_;
        return current_;
    }

private:
    double coef_ = 0.0, current_ = 1.0, target_ = 1.0;
};

/// Linear 0..1 crossfade ramp used when a module is switched on or off.
class EnableRamp {
public:
    void prepare(double sampleRate, double timeMs = 10.0) noexcept {
        step_ = 1.0 / std::max(1.0, 0.001 * timeMs * sampleRate);
    }
    void reset(bool on) noexcept { value_ = target_ = on ? 1.0 : 0.0; }
    void setTarget(bool on) noexcept { target_ = on ? 1.0 : 0.0; }
    [[nodiscard]] bool isActive() const noexcept { return value_ > 0.0 || target_ > 0.0; }
    [[nodiscard]] bool isFullyOn() const noexcept { return value_ >= 1.0 && target_ >= 1.0; }
    [[nodiscard]] bool isFullyOff() const noexcept { return value_ <= 0.0 && target_ <= 0.0; }
    [[nodiscard]] inline double next() noexcept {
        if (value_ < target_)
            value_ = std::min(target_, value_ + step_);
        else if (value_ > target_)
            value_ = std::max(target_, value_ - step_);
        return value_;
    }

private:
    double step_ = 0.01, value_ = 1.0, target_ = 1.0;
};

} // namespace radiff::core::dsp

#pragma once

#include "radiff/core/dsp/Units.h"

#include <cstddef>

namespace radiff::core::dsp {

/// First-order DC blocker: y[n] = x[n] - x[n-1] + R * y[n-1]. Cutoff ~5 Hz by default,
/// which is far below speech and causes no audible phase shift in the voice band.
class DcBlocker {
public:
    void prepare(double sampleRate, double cutoffHz = 5.0) noexcept {
        R_ = 1.0 - (2.0 * kPi * cutoffHz / sampleRate);
        reset();
    }
    void reset() noexcept { x1_ = y1_ = 0.0; }

    [[nodiscard]] inline double processSample(double x) noexcept {
        const double y = x - x1_ + R_ * y1_;
        x1_ = x;
        y1_ = y;
        return y;
    }
    void process(float* s, std::size_t n) noexcept {
        for (std::size_t i = 0; i < n; ++i)
            s[i] = static_cast<float>(processSample(static_cast<double>(s[i])));
    }

private:
    double R_ = 0.999;
    double x1_ = 0.0, y1_ = 0.0;
};

} // namespace radiff::core::dsp

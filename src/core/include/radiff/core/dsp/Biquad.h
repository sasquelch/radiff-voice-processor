#pragma once

#include "radiff/core/dsp/Units.h"

#include <cmath>
#include <cstddef>

namespace radiff::core::dsp {

/// Normalised biquad coefficients (a0 == 1).
struct BiquadCoeffs {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    [[nodiscard]] bool operator==(const BiquadCoeffs&) const noexcept = default;

    /// |H(e^jw)| in dB at `hz` - used by tests and the EQ curve display.
    [[nodiscard]] double magnitudeDb(double hz, double sampleRate) const noexcept {
        const double w = 2.0 * kPi * hz / sampleRate;
        const double c1 = std::cos(w), s1 = std::sin(w);
        const double c2 = std::cos(2 * w), s2 = std::sin(2 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
        const double dr = 1.0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        const double num = nr * nr + ni * ni, den = dr * dr + di * di;
        return 10.0 * std::log10((num + 1e-30) / (den + 1e-30));
    }
};

// Designs follow the Audio EQ Cookbook (R. Bristow-Johnson), a published standard.
// All inputs are clamped to numerically safe ranges; the result is always stable.
namespace design {

[[nodiscard]] inline BiquadCoeffs identity() noexcept { return {}; }

[[nodiscard]] inline BiquadCoeffs lowPass(double fs, double hz, double q) noexcept {
    hz = safeFrequency(hz, fs);
    q = std::clamp(q, 0.05, 20.0);
    const double w0 = 2.0 * kPi * hz / fs, cw = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    return {(1.0 - cw) / 2.0 / a0, (1.0 - cw) / a0, (1.0 - cw) / 2.0 / a0, -2.0 * cw / a0, (1.0 - alpha) / a0};
}

[[nodiscard]] inline BiquadCoeffs highPass(double fs, double hz, double q) noexcept {
    hz = safeFrequency(hz, fs);
    q = std::clamp(q, 0.05, 20.0);
    const double w0 = 2.0 * kPi * hz / fs, cw = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    return {(1.0 + cw) / 2.0 / a0, -(1.0 + cw) / a0, (1.0 + cw) / 2.0 / a0, -2.0 * cw / a0, (1.0 - alpha) / a0};
}

/// Constant 0 dB peak-gain band-pass (RBJ "BPF, constant 0 dB peak gain").
[[nodiscard]] inline BiquadCoeffs bandPass(double fs, double hz, double q) noexcept {
    hz = safeFrequency(hz, fs);
    q = std::clamp(q, 0.05, 20.0);
    const double w0 = 2.0 * kPi * hz / fs, cw = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    return {alpha / a0, 0.0, -alpha / a0, -2.0 * cw / a0, (1.0 - alpha) / a0};
}

/// Q of a band-pass/peaking filter whose -3 dB bandwidth is `octaves` wide (RBJ relation).
[[nodiscard]] inline double qFromBandwidthOctaves(double octaves) noexcept {
    octaves = std::clamp(octaves, 0.05, 10.0);
    return 1.0 / (2.0 * std::sinh(0.5 * std::log(2.0) * octaves));
}

[[nodiscard]] inline BiquadCoeffs peaking(double fs, double hz, double gainDb, double q) noexcept {
    hz = safeFrequency(hz, fs);
    q = std::clamp(q, 0.05, 20.0);
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * hz / fs, cw = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha / A;
    return {(1.0 + alpha * A) / a0, -2.0 * cw / a0, (1.0 - alpha * A) / a0, -2.0 * cw / a0, (1.0 - alpha / A) / a0};
}

[[nodiscard]] inline BiquadCoeffs lowShelf(double fs, double hz, double gainDb, double q) noexcept {
    hz = safeFrequency(hz, fs);
    q = std::clamp(q, 0.05, 20.0);
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * hz / fs, cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2.0 * q), two_sqrtA_alpha = 2.0 * std::sqrt(A) * alpha;
    const double a0 = (A + 1) + (A - 1) * cw + two_sqrtA_alpha;
    return {A * ((A + 1) - (A - 1) * cw + two_sqrtA_alpha) / a0, 2 * A * ((A - 1) - (A + 1) * cw) / a0,
            A * ((A + 1) - (A - 1) * cw - two_sqrtA_alpha) / a0, -2 * ((A - 1) + (A + 1) * cw) / a0,
            ((A + 1) + (A - 1) * cw - two_sqrtA_alpha) / a0};
}

[[nodiscard]] inline BiquadCoeffs highShelf(double fs, double hz, double gainDb, double q) noexcept {
    hz = safeFrequency(hz, fs);
    q = std::clamp(q, 0.05, 20.0);
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * hz / fs, cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2.0 * q), two_sqrtA_alpha = 2.0 * std::sqrt(A) * alpha;
    const double a0 = (A + 1) - (A - 1) * cw + two_sqrtA_alpha;
    return {A * ((A + 1) + (A - 1) * cw + two_sqrtA_alpha) / a0, -2 * A * ((A - 1) + (A + 1) * cw) / a0,
            A * ((A + 1) + (A - 1) * cw - two_sqrtA_alpha) / a0, 2 * ((A - 1) - (A + 1) * cw) / a0,
            ((A + 1) - (A - 1) * cw - two_sqrtA_alpha) / a0};
}

} // namespace design

/// Transposed direct form II biquad with double-precision state and linear coefficient
/// ramping. Changing coefficients never jumps: the new set is reached over `rampSamples`
/// samples (default ~5 ms), which removes the pops that instantaneous updates cause.
class Biquad {
public:
    void prepare(double sampleRate) noexcept {
        rampLength_ = static_cast<int>(std::max(1.0, 0.005 * sampleRate));
        reset();
    }

    void reset() noexcept {
        z1_ = z2_ = 0.0;
        current_ = target_;
        remaining_ = 0;
    }

    /// Sets new coefficients; ramps from the current set unless `immediate`.
    void setCoefficients(const BiquadCoeffs& c, bool immediate = false) noexcept {
        target_ = c;
        if (immediate) {
            current_ = c;
            remaining_ = 0;
            return;
        }
        if (current_ == target_) {
            remaining_ = 0;
            return;
        }
        remaining_ = rampLength_;
        const double inv = 1.0 / rampLength_;
        step_.b0 = (target_.b0 - current_.b0) * inv;
        step_.b1 = (target_.b1 - current_.b1) * inv;
        step_.b2 = (target_.b2 - current_.b2) * inv;
        step_.a1 = (target_.a1 - current_.a1) * inv;
        step_.a2 = (target_.a2 - current_.a2) * inv;
    }

    [[nodiscard]] const BiquadCoeffs& coefficients() const noexcept { return target_; }
    [[nodiscard]] bool isRamping() const noexcept { return remaining_ > 0; }

    [[nodiscard]] inline double processSample(double x) noexcept {
        if (remaining_ > 0) {
            current_.b0 += step_.b0;
            current_.b1 += step_.b1;
            current_.b2 += step_.b2;
            current_.a1 += step_.a1;
            current_.a2 += step_.a2;
            if (--remaining_ == 0)
                current_ = target_;
        }
        const double y = current_.b0 * x + z1_;
        z1_ = current_.b1 * x - current_.a1 * y + z2_;
        z2_ = current_.b2 * x - current_.a2 * y;
        return y;
    }

    void process(float* samples, std::size_t n) noexcept {
        for (std::size_t i = 0; i < n; ++i)
            samples[i] = static_cast<float>(processSample(static_cast<double>(samples[i])));
    }

private:
    BiquadCoeffs current_{}, target_{}, step_{};
    double z1_ = 0.0, z2_ = 0.0;
    int rampLength_ = 240;
    int remaining_ = 0;
};

} // namespace radiff::core::dsp

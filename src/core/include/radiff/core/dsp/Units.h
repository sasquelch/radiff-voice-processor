#pragma once

#include <algorithm>
#include <cmath>

namespace radiff::core::dsp {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMinusInfDb = -200.0;
constexpr double kTinyPower = 1e-20; // -200 dB

[[nodiscard]] inline double dbToGain(double db) noexcept { return std::pow(10.0, db * 0.05); }
[[nodiscard]] inline float dbToGain(float db) noexcept { return std::pow(10.0f, db * 0.05f); }

[[nodiscard]] inline double gainToDb(double gain) noexcept {
    return gain > 1e-10 ? 20.0 * std::log10(gain) : kMinusInfDb;
}
[[nodiscard]] inline double powerToDb(double power) noexcept {
    return power > kTinyPower ? 10.0 * std::log10(power) : kMinusInfDb;
}

/// One-pole coefficient for a time constant in milliseconds (63% step response).
/// 0 ms means "instant" (coefficient 0).
[[nodiscard]] inline double timeConstantCoefficient(double ms, double sampleRate) noexcept {
    if (ms <= 0.0)
        return 0.0;
    return std::exp(-1.0 / (0.001 * ms * sampleRate));
}

[[nodiscard]] inline std::size_t msToSamples(double ms, double sampleRate) noexcept {
    return static_cast<std::size_t>(std::llround(std::max(0.0, ms) * 0.001 * sampleRate));
}

/// Clamps a frequency to a range that is numerically safe for biquad design.
[[nodiscard]] inline double safeFrequency(double hz, double sampleRate) noexcept {
    return std::clamp(hz, 1.0, 0.45 * sampleRate);
}

} // namespace radiff::core::dsp

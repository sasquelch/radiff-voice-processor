#pragma once
// Shared helpers for deterministic DSP measurements.
#include "radiff/core/AudioBuffer.h"

#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace radiff::test {

inline constexpr double kPi = std::numbers::pi;

/// Sine with the given RMS level in dBFS (0 dBFS RMS = amplitude sqrt(2)).
inline core::AudioBuffer sineRms(std::size_t channels, std::size_t frames, double fs, double hz, double rmsDbfs,
                                 double startPhase = 0.0) {
    core::AudioBuffer b(channels, frames);
    const double amp = std::pow(10.0, rmsDbfs / 20.0) * std::sqrt(2.0);
    for (std::size_t c = 0; c < channels; ++c)
        for (std::size_t i = 0; i < frames; ++i)
            b.channel(c)[i] = static_cast<float>(amp * std::sin(2.0 * kPi * hz * static_cast<double>(i) / fs + startPhase));
    return b;
}

/// Sine with the given PEAK amplitude in dBFS.
inline core::AudioBuffer sinePeak(std::size_t channels, std::size_t frames, double fs, double hz, double peakDbfs) {
    return sineRms(channels, frames, fs, hz, peakDbfs - 20.0 * std::log10(std::sqrt(2.0)));
}

/// RMS level in dBFS of frames [start, end).
inline double rmsDb(const core::AudioBuffer& b, std::size_t ch, std::size_t start, std::size_t end) {
    double acc = 0.0;
    for (std::size_t i = start; i < end; ++i)
        acc += static_cast<double>(b.channel(ch)[i]) * static_cast<double>(b.channel(ch)[i]);
    const double ms = acc / static_cast<double>(end - start);
    return ms > 1e-30 ? 10.0 * std::log10(ms) : -300.0;
}

inline double peakDb(const core::AudioBuffer& b, std::size_t ch, std::size_t start, std::size_t end) {
    float p = 0.0f;
    for (std::size_t i = start; i < end; ++i)
        p = std::max(p, std::fabs(b.channel(ch)[i]));
    return p > 1e-15f ? 20.0 * std::log10(static_cast<double>(p)) : -300.0;
}

/// Largest absolute sample-to-sample step (used to detect clicks).
inline double maxStep(const core::AudioBuffer& b, std::size_t ch, std::size_t start, std::size_t end) {
    double m = 0.0;
    for (std::size_t i = start + 1; i < end; ++i)
        m = std::max(m, std::fabs(static_cast<double>(b.channel(ch)[i]) - static_cast<double>(b.channel(ch)[i - 1])));
    return m;
}

/// RMS-equivalent level (dBFS) of the sinusoidal component at `hz` in frames [start, end),
/// by rectangular-window DFT correlation. Exact when the range holds a whole number of cycles
/// of every tone present (e.g. 24000 frames at 48 kHz for 300 Hz and 10 kHz).
inline double toneLevelDb(const core::AudioBuffer& b, std::size_t ch, std::size_t start, std::size_t end, double fs,
                          double hz) {
    double re = 0.0, im = 0.0;
    for (std::size_t i = start; i < end; ++i) {
        const double w = 2.0 * kPi * hz * static_cast<double>(i) / fs;
        const double x = static_cast<double>(b.channel(ch)[i]);
        re += x * std::cos(w);
        im -= x * std::sin(w);
    }
    const double n = static_cast<double>(end - start);
    const double peak = 2.0 * std::sqrt(re * re + im * im) / n;
    return peak > 1e-15 ? 20.0 * std::log10(peak / std::sqrt(2.0)) : -300.0;
}

inline bool allFinite(const core::AudioBuffer& b) {
    for (std::size_t c = 0; c < b.numChannels(); ++c)
        for (float v : b.channel(c))
            if (!std::isfinite(v))
                return false;
    return true;
}

inline double maxAbsDiff(const core::AudioBuffer& a, const core::AudioBuffer& b, std::size_t offsetB = 0) {
    double m = 0.0;
    for (std::size_t c = 0; c < a.numChannels(); ++c)
        for (std::size_t i = 0; i + offsetB < b.numFrames() && i < a.numFrames(); ++i)
            m = std::max(m, std::fabs(static_cast<double>(a.channel(c)[i]) - static_cast<double>(b.channel(c)[i + offsetB])));
    return m;
}

/// Processes `b` through `fn(view)` in blocks of `block` frames.
template <typename Fn> void processInBlocks(core::AudioBuffer& b, std::size_t block, Fn&& fn) {
    for (std::size_t pos = 0; pos < b.numFrames(); pos += block)
        fn(b.view(pos, std::min(block, b.numFrames() - pos)));
}

} // namespace radiff::test

#pragma once

#include <algorithm>
#include <cmath>

namespace radiff::core::dsp {

// Static transfer curves (Giannoulis, Massberg & Reiss, "Digital Dynamic Range
// Compressor Design - A Tutorial and Analysis", JAES 2012), quadratic soft knee.
// Inputs/outputs are in dB; the returned value is the gain to apply (<= 0).

/// Compressor: slope 1/ratio above threshold.
[[nodiscard]] inline double compressorGainDb(double levelDb, double thresholdDb, double ratio, double kneeDb) noexcept {
    const double over = levelDb - thresholdDb;
    const double slope = 1.0 / std::max(1.0, ratio) - 1.0; // <= 0
    if (kneeDb > 0.0 && 2.0 * std::fabs(over) <= kneeDb) {
        const double t = over + kneeDb * 0.5;
        return slope * t * t / (2.0 * kneeDb);
    }
    return over > 0.0 ? slope * over : 0.0;
}

/// Downward expander: slope `ratio` below threshold, capped at -maxAttenuationDb.
[[nodiscard]] inline double expanderGainDb(double levelDb, double thresholdDb, double ratio, double kneeDb,
                                           double maxAttenuationDb) noexcept {
    const double under = levelDb - thresholdDb; // negative below threshold
    const double slope = std::max(1.0, ratio) - 1.0; // >= 0
    double g = 0.0;
    if (kneeDb > 0.0 && 2.0 * std::fabs(under) <= kneeDb) {
        const double t = under - kneeDb * 0.5; // <= 0 inside the knee
        g = -slope * t * t / (2.0 * kneeDb);
    } else if (under < 0.0) {
        g = slope * under;
    }
    return std::max(g, -std::max(0.0, maxAttenuationDb));
}

} // namespace radiff::core::dsp

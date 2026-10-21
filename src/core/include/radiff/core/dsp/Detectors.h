#pragma once

#include "radiff/core/dsp/Units.h"

#include <cmath>

namespace radiff::core::dsp {

/// Level detector feeding the gain computers.
///   RMS : one-pole mean-square with the configured window as time constant
///   Peak: rectified envelope follower with instantaneous attack and a decay time
///         constant equal to the configured window
/// Output is a level in dBFS (sine at 0 dBFS peak reads -3.01 dBFS RMS, 0 dBFS peak).
class LevelDetector {
public:
    enum class Mode { Rms, Peak };

    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        setWindow(windowMs_);
        reset();
    }
    void setMode(Mode m) noexcept { mode_ = m; }
    void setWindow(double ms) noexcept {
        windowMs_ = ms;
        coef_ = timeConstantCoefficient(ms, sampleRate_);
    }
    void reset() noexcept { ms_ = 0.0; env_ = 0.0; }

    /// Feeds one sample, returns the current level in dBFS.
    [[nodiscard]] inline double processSample(double x) noexcept {
        if (mode_ == Mode::Peak) {
            const double a = std::fabs(x);
            env_ = a > env_ ? a : a + coef_ * (env_ - a);
            return gainToDb(env_);
        }
        ms_ = x * x + coef_ * (ms_ - x * x);
        return powerToDb(ms_);
    }

private:
    Mode mode_ = Mode::Rms;
    double sampleRate_ = 48000.0, windowMs_ = 10.0, coef_ = 0.0, ms_ = 0.0, env_ = 0.0;
};

/// Attack/release smoothing of a gain expressed in dB (negative = attenuation).
/// "Attack" is the time constant applied when the gain moves TOWARDS more reduction
/// for a compressor, or towards LESS reduction (opening) for an expander; the owner
/// picks the semantics by calling the appropriate method.
class GainBallistics {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        setTimes(attackMs_, releaseMs_);
        reset();
    }
    void setTimes(double attackMs, double releaseMs) noexcept {
        attackMs_ = attackMs;
        releaseMs_ = releaseMs;
        attackCoef_ = timeConstantCoefficient(attackMs, sampleRate_);
        releaseCoef_ = timeConstantCoefficient(releaseMs, sampleRate_);
    }
    void reset() noexcept { gainDb_ = 0.0; }
    [[nodiscard]] double gainDb() const noexcept { return gainDb_; }
    [[nodiscard]] double releaseCoefficient() const noexcept { return releaseCoef_; }
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }

    /// Compressor semantics: more reduction uses attack, less reduction uses release.
    [[nodiscard]] inline double towardsCompressor(double targetDb, double releaseCoefOverride = -1.0) noexcept {
        const double coef = targetDb < gainDb_ ? attackCoef_ : (releaseCoefOverride >= 0.0 ? releaseCoefOverride : releaseCoef_);
        gainDb_ = targetDb + coef * (gainDb_ - targetDb);
        return gainDb_;
    }
    /// Expander/gate semantics: opening (less reduction) uses attack, closing uses release.
    [[nodiscard]] inline double towardsExpander(double targetDb) noexcept {
        const double coef = targetDb > gainDb_ ? attackCoef_ : releaseCoef_;
        gainDb_ = targetDb + coef * (gainDb_ - targetDb);
        return gainDb_;
    }

private:
    double sampleRate_ = 48000.0, attackMs_ = 10.0, releaseMs_ = 100.0;
    double attackCoef_ = 0.0, releaseCoef_ = 0.0, gainDb_ = 0.0;
};

} // namespace radiff::core::dsp

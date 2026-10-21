#pragma once

#include "radiff/core/AudioBuffer.h"
#include "radiff/core/Parameters.h"
#include "radiff/core/dsp/Biquad.h"
#include "radiff/core/dsp/Detectors.h"
#include "radiff/core/dsp/Smoother.h"

#include <array>

namespace radiff::core::modules {

/// Stage 5: frequency-selective de-esser (dbx-286-class "Frequency" + "Threshold", plus the
/// spec's bandwidth, maximum reduction, attack/release, monitor and stereo link).
///
///  band = BPF(frequencyHz, bandwidthOctaves)(x)        detection band, constant 0 dB peak gain
///  level = peak detector(band) in dBFS (3 ms decay)
///  reduction = min(maxReductionDb, limit(level - threshold)) with a 6 dB soft knee (1:inf above T)
///  attack/release ballistics on the reduction
///  y = x - (1 - g) * band                                 only the detection band is reduced
///
/// At the centre frequency the band-pass has zero phase, so the output there is exactly
/// `g` times the input; away from it the reduction fades out with the band-pass skirt, and
/// material outside the band passes untouched (a 1 kHz tone changes by < 0.1 dB while a 6 kHz
/// band is being reduced by 6 dB, tested). Nothing is ever attenuated by more than
/// maxReductionDb. `listen` outputs the detection band itself (for tuning the frequency) and is
/// never persisted in presets. Off = bit-exact pass-through after a 10 ms crossfade.
class Deesser {
public:
    static constexpr std::size_t kMaxChannels = 2;
    static constexpr double kKneeDb = 6.0;
    static constexpr double kDetectorDecayMs = 3.0;

    void prepare(double sampleRate, std::size_t numChannels);
    void reset() noexcept;
    void update(const ParameterSet& p) noexcept;
    void process(AudioView audio) noexcept;

    /// Largest reduction (positive dB) applied during the last process() call.
    [[nodiscard]] float lastGainReductionDb() const noexcept { return lastGrDb_; }
    /// Reduction (positive dB) the static curve asks for at a detection-band level (dBFS).
    [[nodiscard]] double staticReductionDb(double bandLevelDb) const noexcept;
    [[nodiscard]] const dsp::BiquadCoeffs& bandCoefficients() const noexcept { return bandCoeffs_; }
    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }

private:
    struct ChannelState {
        dsp::Biquad band;
        dsp::LevelDetector detector;
        dsp::GainBallistics ballistics;
    };

    double sampleRate_ = 48000.0;
    std::size_t numChannels_ = 1;
    std::array<ChannelState, kMaxChannels> ch_{};
    dsp::EnableRamp ramp_;
    dsp::BiquadCoeffs bandCoeffs_{};
    bool enabled_ = false, link_ = true, listen_ = false, idle_ = true;
    double thresholdDb_ = -30.0, maxReductionDb_ = 6.0;
    float lastGrDb_ = 0.0f;
    bool firstUpdate_ = true;
};

} // namespace radiff::core::modules

#pragma once

#include "radiff/core/AudioBuffer.h"
#include "radiff/core/Parameters.h"
#include "radiff/core/dsp/Biquad.h"
#include "radiff/core/dsp/Detectors.h"
#include "radiff/core/dsp/Smoother.h"

#include <array>

namespace radiff::core::modules {

/// Stage 8: presence enhancer with dbx-286-class "LF Detail" and "HF Detail" controls.
///
/// What it actually does (no secret sauce):
///  * LF Detail 0..10 -> low shelf at lfFrequencyHz, +0.6 dB per step (max +6 dB), Q 0.707.
///  * HF Detail 0..10 -> high shelf at hfFrequencyHz, +0.8 dB per step (max +8 dB), Q 0.707.
///  * hfDynamic 0..1 makes the HF boost program dependent: the input's level above
///    hfFrequencyHz (2nd-order HPF, 10 ms RMS) is compared with its wideband level; when the
///    high band already dominates (sibilance, mouth clicks, hiss) the boost is scaled down by
///    up to `hfDynamic` x 100 %, linearly over the last 12 dB of dominance. The shelf gain is
///    smoothed (30 ms) and the filter re-designed every 32 samples with coefficient ramping,
///    so there is no zipper noise. hfDynamic = 0 gives a static shelf.
///  * makeupGainDb: explicit compensating gain, smoothed. No automatic loudness change.
/// Detection taps the module input, so the boost never feeds its own detector.
/// Off = bit-exact pass-through after a 10 ms crossfade.
class Presence {
public:
    static constexpr std::size_t kMaxChannels = 2;
    static constexpr double kLfDbPerStep = 0.6, kHfDbPerStep = 0.8;
    static constexpr double kDominanceRangeDb = 12.0;
    static constexpr double kDetectorMs = 10.0, kGainSmoothMs = 30.0;
    static constexpr std::size_t kUpdateInterval = 32;

    void prepare(double sampleRate, std::size_t numChannels);
    void reset() noexcept;
    void update(const ParameterSet& p) noexcept;
    void process(AudioView audio) noexcept;

    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }
    [[nodiscard]] double lfGainDb() const noexcept { return lfGainDb_; }
    [[nodiscard]] double hfStaticGainDb() const noexcept { return hfStaticDb_; }
    /// HF shelf gain currently applied (after the dynamic scaling), for meters/tests.
    [[nodiscard]] double hfCurrentGainDb() const noexcept { return hfCurrentDb_; }
    /// Static magnitude response in dB at `hz` (HF shelf at its static gain), for displays.
    [[nodiscard]] double magnitudeDb(double hz) const noexcept;

private:
    struct ChannelState {
        dsp::Biquad lf, hf, detectorHp;
        dsp::LevelDetector hfLevel, wideLevel;
    };

    void designHf(double gainDb, bool immediate) noexcept;

    double sampleRate_ = 48000.0;
    std::size_t numChannels_ = 1;
    std::array<ChannelState, kMaxChannels> ch_{};
    dsp::EnableRamp ramp_;
    dsp::OnePoleSmoother makeup_, hfGain_;
    dsp::BiquadCoeffs lfCoeffs_{}, hfStaticCoeffs_{};
    bool enabled_ = false, idle_ = true, firstUpdate_ = true;
    double lfHz_ = 100.0, lfGainDb_ = 0.0, hfHz_ = 3500.0, hfStaticDb_ = 0.0, dynamic_ = 0.5;
    double hfCurrentDb_ = 0.0, hfDesignedDb_ = 0.0;
    std::size_t counter_ = 0;
};

} // namespace radiff::core::modules

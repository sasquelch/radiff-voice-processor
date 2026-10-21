#pragma once

#include "radiff/core/AudioBuffer.h"
#include "radiff/core/Parameters.h"
#include "radiff/core/dsp/Biquad.h"
#include "radiff/core/dsp/Detectors.h"
#include "radiff/core/dsp/Smoother.h"

#include <array>

namespace radiff::core::modules {

/// Stages 6/7: feed-forward compressor used twice (leveling, then density).
///
///  detector (RMS/peak, optional sidechain HPF) -> level dB
///  static curve (threshold, ratio, soft knee)   -> target gain
///  attack/release ballistics                    -> smoothed gain
///  optional program-dependent release: the release time constant stretches up to 4x
///  as the long-term (~1 s) average reduction deepens, so brief peaks recover quickly
///  while sustained loud passages release gently.
///  makeup gain (smoothed) and wet/dry mix on the output. With `autoMakeup` the makeup is
///  increased by the static gain reduction a kAutoMakeupReferenceDb (-20 dBFS RMS) signal would
///  receive, so a nominal-level voice leaves at the level it entered (dbx-286 "Drive" style);
///  explicit opt-in, never hidden.
class Compressor {
public:
    static constexpr std::size_t kMaxChannels = 2;
    static constexpr double kAutoMakeupReferenceDb = -20.0;

    /// `which` selects Compressor1 or Compressor2 parameters from the shared registry.
    explicit Compressor(Module which = Module::Compressor1) noexcept;

    void prepare(double sampleRate, std::size_t numChannels);
    void reset() noexcept;
    void update(const ParameterSet& p) noexcept;
    void process(AudioView audio) noexcept;

    [[nodiscard]] float lastGainReductionDb() const noexcept { return lastGrDb_; }
    [[nodiscard]] double staticGainDb(double levelDb) const noexcept;
    [[nodiscard]] double currentReleaseMultiplier() const noexcept { return releaseMultiplier_; }
    /// Extra makeup (dB) contributed by autoMakeup (0 when off).
    [[nodiscard]] double autoMakeupDb() const noexcept { return autoMakeupDb_; }

private:
    struct Ids {
        ParamId enabled, threshold, ratio, knee, attack, release, makeup, detector, rmsWindow, scHpEnabled, scHpHz,
            autoRelease, wet, stereoLink, autoMakeup;
    };
    struct ChannelState {
        dsp::LevelDetector detector;
        dsp::Biquad scHp;
        dsp::GainBallistics ballistics;
    };

    [[nodiscard]] inline double smoothedGain(ChannelState& s, double targetDb) noexcept;

    Ids ids_;
    double sampleRate_ = 48000.0;
    std::size_t numChannels_ = 1;
    std::array<ChannelState, kMaxChannels> ch_{};
    dsp::OnePoleSmoother makeup_, wet_;

    bool enabled_ = true, link_ = true, scHpOn_ = false, autoRelease_ = false, autoMakeup_ = false;
    double autoMakeupDb_ = 0.0;
    double thresholdDb_ = -24.0, ratio_ = 2.5, kneeDb_ = 6.0;
    double longTermGr_ = 0.0, longTermCoef_ = 0.0, releaseCoef_ = 0.0, releaseMs_ = 150.0, releaseMultiplier_ = 1.0;
    float lastGrDb_ = 0.0f;
    bool firstUpdate_ = true;
};

} // namespace radiff::core::modules

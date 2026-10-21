#pragma once

#include "radiff/core/AudioBuffer.h"
#include "radiff/core/Parameters.h"
#include "radiff/core/dsp/Biquad.h"
#include "radiff/core/dsp/DelayLine.h"
#include "radiff/core/dsp/Detectors.h"
#include "radiff/core/dsp/Smoother.h"

#include <array>

namespace radiff::core::modules {

/// Stage 3: speech-preserving downward expander.
///
///  detector (RMS/peak, optional sidechain HP/LP) -> level dB
///  open/closed state with hysteresis and hold      -> target gain (0 dB when open)
///  static curve below threshold (soft knee, ratio) -> capped at -maxAttenuationDb
///  attack (opening) / release (closing) ballistics -> smoothed gain
///  gain applied to the audio delayed by `lookahead` samples; wet/dry mix on the output
///
/// The lookahead delay is ALWAYS applied (even when the module is disabled) so that the
/// reported latency only depends on the lookahead parameter, never on enable state.
class Expander {
public:
    static constexpr std::size_t kMaxChannels = 2;
    static constexpr double kMaxLookaheadMs = 20.0;

    void prepare(double sampleRate, std::size_t numChannels);
    void reset() noexcept;
    void update(const ParameterSet& p) noexcept;
    void process(AudioView audio) noexcept;

    [[nodiscard]] std::size_t latencySamples() const noexcept { return lookaheadSamples_; }
    /// Largest attenuation (positive dB) applied during the last process() call.
    [[nodiscard]] float lastGainReductionDb() const noexcept { return lastGrDb_; }
    /// Static transfer curve helper for displays/tests: gain (dB) for an input level (dBFS).
    [[nodiscard]] double staticGainDb(double levelDb) const noexcept;

private:
    struct ChannelState {
        dsp::LevelDetector detector;
        dsp::Biquad scHp, scLp;
        dsp::GainBallistics ballistics;
        dsp::DelayLine delay;
        bool open = true;
        std::size_t holdRemaining = 0;
    };

    [[nodiscard]] inline double targetGainFor(ChannelState& s, double levelDb) noexcept;

    double sampleRate_ = 48000.0;
    std::size_t numChannels_ = 1;
    std::array<ChannelState, kMaxChannels> ch_{};
    dsp::OnePoleSmoother wet_;

    bool enabled_ = true, link_ = true, scHpOn_ = false, scLpOn_ = false;
    double thresholdDb_ = -48.0, ratio_ = 2.0, kneeDb_ = 6.0, hysteresisDb_ = 4.0, maxAttenDb_ = 10.0;
    std::size_t holdSamples_ = 0, lookaheadSamples_ = 0;
    float lastGrDb_ = 0.0f;
    bool firstUpdate_ = true;
};

} // namespace radiff::core::modules

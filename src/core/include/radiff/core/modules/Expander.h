#pragma once

#include "radiff/core/AudioBuffer.h"
#include "radiff/core/Parameters.h"
#include "radiff/core/dsp/Biquad.h"
#include "radiff/core/dsp/DelayLine.h"
#include "radiff/core/dsp/Detectors.h"
#include "radiff/core/dsp/Smoother.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace radiff::core::modules {

/// Stage 3: speech-preserving downward expander.
///
///  detector (RMS/peak, optional sidechain HP/LP) -> level dB
///  open/closed state with hysteresis and hold      -> target gain (0 dB when open)
///  static curve below threshold (soft knee, ratio) -> capped at -maxAttenuationDb
///  attack (opening) / release (closing) ballistics -> smoothed gain
///  gain applied to the audio delayed by `delay` samples; wet/dry mix on the output
///
/// Threshold: either the absolute `thresholdDbfs`, or (mode speechRelative) the measured speech
/// level of the material plus `relativeThresholdDb`. The measurement comes from the host via
/// setSpeechLevelDb(); without one the absolute threshold is used.
///
/// Transient rejection: with `transientRejectMs` > 0 a closed expander only opens once a fast
/// (2 ms RMS) auxiliary detector has been above the threshold for at least two thirds of the last
/// `transientRejectMs` (cumulative, so a plosive burst followed by a vowel still qualifies).
/// Short isolated transients (mouse clicks, keyboard) in pauses therefore never open it, and
/// while such an unverified burst rings through the main detector the closed-state attenuation
/// is held at the level that preceded the burst. The internal delay of
/// max(lookahead, transientReject) means a genuine word onset is still passed intact
/// (the gain is already opening when the onset reaches the output). Rejection only ever
/// affects material below the threshold; an open expander is unaffected.
///
/// The delay is ALWAYS applied (even when the module is disabled) so that the reported latency
/// only depends on the lookahead/transient-reject parameters, never on enable state.
class Expander {
public:
    static constexpr std::size_t kMaxChannels = 2;
    static constexpr double kMaxLookaheadMs = 20.0;

    void prepare(double sampleRate, std::size_t numChannels);
    void reset() noexcept;
    void update(const ParameterSet& p) noexcept;
    void process(AudioView audio) noexcept;

    /// Host-supplied speech level (dBFS, detector domain) for the speechRelative threshold mode.
    /// nullopt = unknown (absolute threshold is used). Realtime-safe.
    void setSpeechLevelDb(std::optional<double> levelDb) noexcept;
    [[nodiscard]] std::optional<double> speechLevelDb() const noexcept { return speechLevelDb_; }

    [[nodiscard]] std::size_t latencySamples() const noexcept { return delaySamples_; }
    /// Largest attenuation (positive dB) applied during the last process() call.
    [[nodiscard]] float lastGainReductionDb() const noexcept { return lastGrDb_; }
    /// The threshold actually in use (dBFS) after resolving the threshold mode.
    [[nodiscard]] double effectiveThresholdDb() const noexcept { return thresholdDb_; }
    /// Static transfer curve helper for displays/tests: gain (dB) for an input level (dBFS).
    [[nodiscard]] double staticGainDb(double levelDb) const noexcept;

private:
    static constexpr double kFastWindowMs = 2.0;        ///< auxiliary verification detector
    static constexpr double kVerifyHysteresisDb = 3.0;  ///< ripple tolerance while verifying
    static constexpr double kFreezeMs = 250.0;          ///< max hold of the pre-burst level
    static constexpr double kTrackMs = 20.0;            ///< upward time constant of the pre-burst tracker

    struct ChannelState {
        dsp::LevelDetector detector, fastDetector;
        dsp::Biquad scHp, scLp;
        dsp::GainBallistics ballistics;
        dsp::DelayLine delay;
        bool open = true;
        std::size_t holdRemaining = 0;
        std::size_t openSamples = 0;     ///< how long the current open period has lasted
        std::vector<std::uint8_t> aboveRing; ///< per-sample "fast >= threshold" flags, last N samples
        std::size_t ringPos = 0;
        std::size_t aboveCount = 0;      ///< number of set flags in the ring (cumulative time above)
        bool lastAbove = false;
        std::size_t freezeRemaining = 0; ///< samples left holding closedLevelDb after an unverified burst
        double closedLevelDb = -144.0;   ///< level that preceded the current/last burst
    };

    [[nodiscard]] inline double targetGainFor(ChannelState& s, double levelDb, double fastDb) noexcept;
    void resolveThreshold() noexcept;

    double sampleRate_ = 48000.0;
    std::size_t numChannels_ = 1;
    std::array<ChannelState, kMaxChannels> ch_{};
    dsp::OnePoleSmoother wet_;

    bool enabled_ = true, link_ = true, scHpOn_ = false, scLpOn_ = false;
    ThresholdMode thresholdMode_ = ThresholdMode::Absolute;
    double absoluteThresholdDb_ = -48.0, relativeThresholdDb_ = -15.0;
    std::optional<double> speechLevelDb_;
    double thresholdDb_ = -48.0, ratio_ = 2.0, kneeDb_ = 6.0, hysteresisDb_ = 4.0, maxAttenDb_ = 10.0;
    std::size_t holdSamples_ = 0, lookaheadSamples_ = 0, rejectSamples_ = 0, delaySamples_ = 0, freezeSamples_ = 0;
    std::size_t verifySamples_ = 0; ///< flags needed within the ring to open (2/3 of rejectSamples_)
    double trackCoef_ = 0.0;
    float lastGrDb_ = 0.0f;
    bool firstUpdate_ = true;
};

} // namespace radiff::core::modules

#pragma once

#include "radiff/core/AudioBuffer.h"
#include "radiff/core/Parameters.h"
#include "radiff/core/dsp/Biquad.h"
#include "radiff/core/dsp/DcBlocker.h"
#include "radiff/core/dsp/Smoother.h"

#include <array>

namespace radiff::core::modules {

/// Stage 1: input trim, optional DC blocker, optional 12/24 dB/oct Butterworth high-pass.
/// Gain changes are smoothed; filter toggles crossfade; coefficient changes ramp.
class InputStage {
public:
    static constexpr std::size_t kMaxChannels = 2;

    void prepare(double sampleRate, std::size_t numChannels);
    void reset() noexcept;
    void update(const ParameterSet& p) noexcept;
    void process(AudioView audio) noexcept;

    /// Combined magnitude response of the enabled filters (for display/tests).
    [[nodiscard]] double magnitudeDb(double hz) const noexcept;

private:
    double sampleRate_ = 48000.0;
    std::size_t numChannels_ = 1;
    dsp::OnePoleSmoother gain_;
    dsp::EnableRamp dcRamp_, hpfRamp_;
    bool dcEnabled_ = true, hpfEnabled_ = true;
    int hpfStages_ = 1;
    std::array<dsp::DcBlocker, kMaxChannels> dc_{};
    std::array<std::array<dsp::Biquad, 2>, kMaxChannels> hpf_{}; // up to 2 cascaded biquads
    bool firstUpdate_ = true;
};

} // namespace radiff::core::modules

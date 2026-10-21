#pragma once

#include "radiff/core/AudioBuffer.h"
#include "radiff/core/Parameters.h"
#include "radiff/core/dsp/Biquad.h"
#include "radiff/core/dsp/Smoother.h"

#include <array>

namespace radiff::core::modules {

/// Stage 2: five-band parametric EQ (bell, low/high shelf, high/low pass per band).
/// Bands are cascaded biquads; disabled bands are skipped; module and band toggles
/// crossfade so no clicks occur; coefficient changes ramp over ~5 ms.
class Equalizer {
public:
    static constexpr std::size_t kMaxChannels = 2;

    void prepare(double sampleRate, std::size_t numChannels);
    void reset() noexcept;
    void update(const ParameterSet& p) noexcept;
    void process(AudioView audio) noexcept;

    /// Magnitude response of the whole EQ in dB at `hz` (as currently configured).
    [[nodiscard]] double magnitudeDb(double hz) const noexcept;
    [[nodiscard]] static dsp::BiquadCoeffs designBand(double sampleRate, EqBandType type, double hz, double gainDb,
                                                      double q) noexcept;

private:
    double sampleRate_ = 48000.0;
    std::size_t numChannels_ = 1;
    bool enabled_ = true;
    dsp::EnableRamp moduleRamp_;
    std::array<bool, kNumEqBands> bandEnabled_{};
    std::array<dsp::EnableRamp, kNumEqBands> bandRamp_{};
    std::array<std::array<dsp::Biquad, kNumEqBands>, kMaxChannels> filters_{};
    bool firstUpdate_ = true;
};

} // namespace radiff::core::modules

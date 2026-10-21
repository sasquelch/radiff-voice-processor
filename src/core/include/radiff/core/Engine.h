#pragma once

#include "radiff/core/AudioBuffer.h"
#include "radiff/core/Parameters.h"
#include "radiff/core/dsp/DelayLine.h"
#include "radiff/core/dsp/Smoother.h"
#include "radiff/core/modules/Compressor.h"
#include "radiff/core/modules/Equalizer.h"
#include "radiff/core/modules/Expander.h"
#include "radiff/core/modules/InputStage.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <vector>

namespace radiff::core {

/// Processing configuration fixed for the lifetime of a prepare()/release() cycle.
struct EngineConfig {
    double sampleRate = 48000.0;
    std::size_t maxBlockFrames = 512;
    std::size_t numChannels = 1; ///< 1 = mono, 2 = stereo
};

/// Meter snapshot: peaks are linear (last block), reductions are positive dB (last block).
struct Meters {
    std::array<float, 2> inputPeak{};
    std::array<float, 2> outputPeak{};
    float expanderReductionDb = 0.0f;
    float compressor1ReductionDb = 0.0f;
    float compressor2ReductionDb = 0.0f;
};

/// Algorithm version: bump whenever a change alters the audible transfer function so
/// presets/sessions can record which algorithm produced a result.
constexpr int kAlgorithmVersion = 1;

/// The single shared DSP engine used by the CLI, standalone app, VST3 and AU builds.
///
/// Signal order (fixed, v1): input stage -> EQ -> expander -> compressor 1 ->
/// compressor 2 -> output trim. Stages not yet implemented from the master spec
/// (multiband expander, de-esser, presence, saturation, limiter) are added in Phase 6.
///
/// Threading contract (identical to every plug-in API):
///   * prepare()/release()/reset() are called from a non-realtime thread.
///   * setParameters() and process() may be called from the audio thread; neither
///     allocates, locks, throws or touches the filesystem.
///   * meters() may be read from any thread (relaxed atomics).
class Engine {
public:
    static constexpr std::size_t kMaxChannels = 2;

    Engine();

    void prepare(const EngineConfig& config);
    void release() noexcept;
    void reset() noexcept;

    [[nodiscard]] const EngineConfig& config() const noexcept { return config_; }
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    /// Algorithmic latency in frames at the current sample rate (expander lookahead).
    /// Constant for a given parameter set; hosts are told when it changes.
    [[nodiscard]] std::size_t latencyFrames() const noexcept { return latency_; }

    void setParameters(const ParameterSet& params) noexcept;
    [[nodiscard]] const ParameterSet& parameters() const noexcept { return params_; }

    /// Processes `numFrames` frames in place. `numFrames` must be <= maxBlockFrames and
    /// `audio.numChannels` must equal the prepared channel count.
    void process(AudioView audio) noexcept;

    [[nodiscard]] Meters meters() const noexcept;

    // Direct access for displays/tests (read-only).
    [[nodiscard]] const modules::InputStage& inputStage() const noexcept { return input_; }
    [[nodiscard]] const modules::Equalizer& equalizer() const noexcept { return eq_; }
    [[nodiscard]] const modules::Expander& expander() const noexcept { return expander_; }
    [[nodiscard]] const modules::Compressor& compressor1() const noexcept { return comp1_; }
    [[nodiscard]] const modules::Compressor& compressor2() const noexcept { return comp2_; }

private:
    void applyParameters() noexcept;

    EngineConfig config_{};
    ParameterSet params_{};
    bool prepared_ = false;
    bool paramsDirty_ = true;
    bool hasProcessed_ = false; ///< false until the first block after prepare()/reset()
    std::size_t latency_ = 0;

    modules::InputStage input_;
    modules::Equalizer eq_;
    modules::Expander expander_;
    modules::Compressor comp1_{Module::Compressor1};
    modules::Compressor comp2_{Module::Compressor2};
    dsp::OnePoleSmoother outputGain_;

    // Full-chain bypass keeps the signal time-aligned with the processed path.
    std::array<dsp::DelayLine, kMaxChannels> bypassDelay_{};
    dsp::EnableRamp bypassRamp_; // 1 = processing, 0 = bypassed
    bool bypassed_ = false;
    std::vector<float> dry_; ///< maxBlockFrames * channels, allocated in prepare()

    void processBlock(AudioView audio) noexcept;

    std::array<std::atomic<float>, 2> inPeak_{}, outPeak_{};
    std::atomic<float> expGr_{0.0f}, c1Gr_{0.0f}, c2Gr_{0.0f};
};

} // namespace radiff::core

#pragma once

#include "radiff/core/AudioBuffer.h"
#include "radiff/core/Parameters.h"
#include "radiff/core/dsp/DelayLine.h"
#include "radiff/core/dsp/Smoother.h"
#include "radiff/core/Analysis.h"
#include "radiff/core/modules/Compressor.h"
#include "radiff/core/modules/Deesser.h"
#include "radiff/core/modules/Equalizer.h"
#include "radiff/core/modules/Presence.h"
#include "radiff/core/modules/Expander.h"
#include "radiff/core/modules/InputStage.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
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
    float deesserReductionDb = 0.0f;
    float compressor1ReductionDb = 0.0f;
    float compressor2ReductionDb = 0.0f;
};

/// Algorithm version: bump whenever a change alters the audible transfer function so
/// presets/sessions can record which algorithm produced a result.
constexpr int kAlgorithmVersion = 1;

/// The single shared DSP engine used by the CLI, standalone app, VST3 and AU builds.
///
/// Signal order (fixed, v1): input stage -> EQ -> expander -> de-esser -> compressor 1 ->
/// compressor 2 -> presence/enhancer -> output trim. Stages not yet implemented from the
/// master spec (multiband expander, saturation, limiter) have no parameters yet.
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

    /// Measured speech level of the material (dBFS, detector domain; see core::LevelAnalyzer),
    /// used by the expander's speechRelative threshold mode. nullopt = unknown. Not part of the
    /// preset (it describes the recording, not the sound). Realtime-safe.
    void setSpeechLevelDb(std::optional<double> levelDb) noexcept;
    [[nodiscard]] std::optional<double> speechLevelDb() const noexcept { return speechLevelDb_; }

    /// Online speech-level tracking for realtime hosts (standalone live input, plug-ins): the
    /// post-EQ signal is measured with a LevelAnalyzer that forgets with time constant
    /// `windowSeconds`, and once at least kMinTrackedSeconds of gated speech have been seen the
    /// expander's speechRelative threshold follows the tracked level (refreshed every 100 ms).
    /// Until then (and when disabled) the host-supplied value from setSpeechLevelDb() applies,
    /// or the absolute threshold if none. Not realtime-safe to toggle (resets the tracker).
    void setSpeechTracking(bool enabled, double windowSeconds = 20.0) noexcept;
    [[nodiscard]] bool speechTracking() const noexcept { return tracking_; }
    /// Last tracked speech level (dBFS), nullopt until enough speech has been heard.
    [[nodiscard]] std::optional<double> trackedSpeechLevelDb() const noexcept;
    static constexpr double kMinTrackedSeconds = 1.0;

    /// Processes `numFrames` frames in place. `numFrames` must be <= maxBlockFrames and
    /// `audio.numChannels` must equal the prepared channel count.
    void process(AudioView audio) noexcept;

    [[nodiscard]] Meters meters() const noexcept;

    // Direct access for displays/tests (read-only).
    [[nodiscard]] const modules::InputStage& inputStage() const noexcept { return input_; }
    [[nodiscard]] const modules::Equalizer& equalizer() const noexcept { return eq_; }
    [[nodiscard]] const modules::Expander& expander() const noexcept { return expander_; }
    [[nodiscard]] const modules::Deesser& deesser() const noexcept { return deesser_; }
    [[nodiscard]] const modules::Compressor& compressor1() const noexcept { return comp1_; }
    [[nodiscard]] const modules::Compressor& compressor2() const noexcept { return comp2_; }
    [[nodiscard]] const modules::Presence& presence() const noexcept { return presence_; }

private:
    void applyParameters() noexcept;
    void pushSpeechLevel() noexcept;

    EngineConfig config_{};
    ParameterSet params_{};
    bool prepared_ = false;
    bool paramsDirty_ = true;
    bool hasProcessed_ = false; ///< false until the first block after prepare()/reset()
    std::optional<double> speechLevelDb_;
    std::size_t latency_ = 0;

    modules::InputStage input_;
    modules::Equalizer eq_;
    modules::Expander expander_;
    modules::Deesser deesser_;
    modules::Compressor comp1_{Module::Compressor1};
    modules::Compressor comp2_{Module::Compressor2};
    modules::Presence presence_;
    dsp::OnePoleSmoother outputGain_;

    // Online speech-level tracker (see setSpeechTracking).
    LevelAnalyzer tracker_;
    bool tracking_ = false;
    double trackWindowSeconds_ = 20.0;
    std::size_t trackCounter_ = 0, trackInterval_ = 4800;
    std::optional<double> trackedLevelDb_;
    std::atomic<float> trackedLevelAtomic_{-1000.0f}; ///< < -999 = none (readable from any thread)

    // Full-chain bypass keeps the signal time-aligned with the processed path.
    std::array<dsp::DelayLine, kMaxChannels> bypassDelay_{};
    dsp::EnableRamp bypassRamp_; // 1 = processing, 0 = bypassed
    bool bypassed_ = false;
    std::vector<float> dry_; ///< maxBlockFrames * channels, allocated in prepare()

    void processBlock(AudioView audio) noexcept;

    std::array<std::atomic<float>, 2> inPeak_{}, outPeak_{};
    std::atomic<float> expGr_{0.0f}, dsGr_{0.0f}, c1Gr_{0.0f}, c2Gr_{0.0f};
};

} // namespace radiff::core

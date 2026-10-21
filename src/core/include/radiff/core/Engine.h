#pragma once

#include "radiff/core/AudioBuffer.h"
#include "radiff/core/Parameters.h"

#include <cstddef>

namespace radiff::core {

/// Processing configuration fixed for the lifetime of a prepare()/release() cycle.
struct EngineConfig {
    double sampleRate = 48000.0;
    std::size_t maxBlockFrames = 512;
    std::size_t numChannels = 1; ///< 1 = mono, 2 = stereo (dual-mono processing)
};

/// The single shared DSP engine used by the CLI, standalone app, VST3 and AU builds.
///
/// Threading contract (identical to every plug-in API):
///   * prepare()/release()/reset() are called from a non-realtime thread.
///   * setParameters() and process() may be called from the audio thread; neither
///     allocates, locks, throws or touches the filesystem.
///
/// PHASE 2 NOTE: this is the architectural skeleton. The processing chain defined in
/// the master specification will be implemented as composable stages inside this
/// class; the public interface is intended to stay as it is.
class Engine {
public:
    Engine();

    void prepare(const EngineConfig& config);
    void release() noexcept;
    void reset() noexcept;

    [[nodiscard]] const EngineConfig& config() const noexcept { return config_; }
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    /// Algorithmic latency in frames at the current sample rate, reported to hosts.
    [[nodiscard]] std::size_t latencyFrames() const noexcept { return 0; }

    void setParameters(const ParameterSet& params) noexcept;
    [[nodiscard]] const ParameterSet& parameters() const noexcept { return params_; }

    /// Processes `numFrames` frames in place. `numFrames` must be <= maxBlockFrames and
    /// `audio.numChannels` must equal the prepared channel count.
    void process(AudioView audio) noexcept;

private:
    static float dbToGain(float db) noexcept;
    void updateDerived() noexcept;

    EngineConfig config_{};
    ParameterSet params_{};
    bool prepared_ = false;

    // Derived, audio-thread-only state.
    float inputGain_ = 1.0f;
    float outputGain_ = 1.0f;
};

} // namespace radiff::core

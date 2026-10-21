#include "radiff/core/Engine.h"
#include "radiff/core/dsp/Denormals.h"
#include "radiff/core/dsp/Units.h"

#include <algorithm>
#include <cmath>

namespace radiff::core {

Engine::Engine() = default;

void Engine::prepare(const EngineConfig& config) {
    config_ = config;
    config_.numChannels = std::clamp<std::size_t>(config.numChannels, 1, kMaxChannels);
    config_.maxBlockFrames = std::max<std::size_t>(1, config.maxBlockFrames);

    input_.prepare(config_.sampleRate, config_.numChannels);
    eq_.prepare(config_.sampleRate, config_.numChannels);
    expander_.prepare(config_.sampleRate, config_.numChannels);
    deesser_.prepare(config_.sampleRate, config_.numChannels);
    comp1_.prepare(config_.sampleRate, config_.numChannels);
    comp2_.prepare(config_.sampleRate, config_.numChannels);
    presence_.prepare(config_.sampleRate, config_.numChannels);
    outputGain_.prepare(config_.sampleRate, 10.0);
    tracker_.prepare(config_.sampleRate, config_.numChannels);
    tracker_.setForgetting(tracking_ ? trackWindowSeconds_ : 0.0);
    trackInterval_ = std::max<std::size_t>(1, dsp::msToSamples(100.0, config_.sampleRate));
    trackCounter_ = 0;
    trackedLevelDb_.reset();
    trackedLevelAtomic_.store(-1000.0f, std::memory_order_relaxed);
    const std::size_t maxDelay = dsp::msToSamples(modules::Expander::kMaxLookaheadMs, config_.sampleRate) + 1;
    for (auto& d : bypassDelay_)
        d.prepare(maxDelay);
    bypassRamp_.prepare(config_.sampleRate, 10.0);
    dry_.assign(config_.maxBlockFrames * kMaxChannels, 0.0f);
    pushSpeechLevel();

    prepared_ = true;
    paramsDirty_ = true;
    applyParameters();
    bypassRamp_.reset(!bypassed_);
    reset();
}

void Engine::release() noexcept { prepared_ = false; }

void Engine::reset() noexcept {
    hasProcessed_ = false;
    input_.reset();
    eq_.reset();
    expander_.reset();
    deesser_.reset();
    comp1_.reset();
    comp2_.reset();
    presence_.reset();
    outputGain_.reset(outputGain_.target());
    for (auto& d : bypassDelay_)
        d.reset();
    bypassRamp_.reset(!bypassed_);
    for (auto& a : inPeak_)
        a.store(0.0f, std::memory_order_relaxed);
    for (auto& a : outPeak_)
        a.store(0.0f, std::memory_order_relaxed);
    expGr_.store(0.0f, std::memory_order_relaxed);
    dsGr_.store(0.0f, std::memory_order_relaxed);
    c1Gr_.store(0.0f, std::memory_order_relaxed);
    c2Gr_.store(0.0f, std::memory_order_relaxed);
}

void Engine::setParameters(const ParameterSet& params) noexcept {
    if (params == params_ && !paramsDirty_)
        return;
    params_ = params;
    paramsDirty_ = true;
    if (prepared_) {
        applyParameters();
        // Before any audio has flowed there is nothing to smooth from: jump straight to
        // the new values so offline renders and plug-in state restores start settled.
        if (!hasProcessed_)
            reset();
    }
}

void Engine::setSpeechLevelDb(std::optional<double> levelDb) noexcept {
    speechLevelDb_ = levelDb;
    pushSpeechLevel();
}

void Engine::pushSpeechLevel() noexcept {
    expander_.setSpeechLevelDb(trackedLevelDb_ ? trackedLevelDb_ : speechLevelDb_);
}

void Engine::setSpeechTracking(bool enabled, double windowSeconds) noexcept {
    tracking_ = enabled;
    trackWindowSeconds_ = std::max(1.0, windowSeconds);
    tracker_.setForgetting(tracking_ ? trackWindowSeconds_ : 0.0);
    tracker_.reset();
    trackCounter_ = 0;
    trackedLevelDb_.reset();
    trackedLevelAtomic_.store(-1000.0f, std::memory_order_relaxed);
    pushSpeechLevel();
}

std::optional<double> Engine::trackedSpeechLevelDb() const noexcept {
    const float v = trackedLevelAtomic_.load(std::memory_order_relaxed);
    if (v < -999.0f)
        return std::nullopt;
    return static_cast<double>(v);
}

void Engine::applyParameters() noexcept {
    if (!paramsDirty_)
        return;
    paramsDirty_ = false;
    input_.update(params_);
    eq_.update(params_);
    expander_.update(params_);
    pushSpeechLevel();
    deesser_.update(params_);
    comp1_.update(params_);
    comp2_.update(params_);
    presence_.update(params_);
    outputGain_.setTarget(dsp::dbToGain(static_cast<double>(params_.get(ParamId::OutputGainDb))));
    latency_ = expander_.latencySamples();
    for (auto& d : bypassDelay_)
        d.setDelay(latency_);
    bypassed_ = params_.getBool(ParamId::Bypass);
    bypassRamp_.setTarget(!bypassed_);
}

Meters Engine::meters() const noexcept {
    Meters m;
    for (std::size_t c = 0; c < 2; ++c) {
        m.inputPeak[c] = inPeak_[c].load(std::memory_order_relaxed);
        m.outputPeak[c] = outPeak_[c].load(std::memory_order_relaxed);
    }
    m.expanderReductionDb = expGr_.load(std::memory_order_relaxed);
    m.deesserReductionDb = dsGr_.load(std::memory_order_relaxed);
    m.compressor1ReductionDb = c1Gr_.load(std::memory_order_relaxed);
    m.compressor2ReductionDb = c2Gr_.load(std::memory_order_relaxed);
    return m;
}

void Engine::process(AudioView audio) noexcept {
    if (!prepared_ || audio.numFrames == 0)
        return;
    const dsp::ScopedFlushDenormals ftz;
    // Hosts must respect maxBlockFrames, but be robust: split oversized calls.
    std::array<float*, kMaxChannels> ptrs{};
    const std::size_t nch = std::min(audio.numChannels, config_.numChannels);
    for (std::size_t pos = 0; pos < audio.numFrames; pos += config_.maxBlockFrames) {
        const std::size_t n = std::min(config_.maxBlockFrames, audio.numFrames - pos);
        for (std::size_t c = 0; c < nch; ++c)
            ptrs[c] = audio.channels[c] + pos;
        processBlock(AudioView{ptrs.data(), nch, n});
    }
}

void Engine::processBlock(AudioView audio) noexcept {
    const std::size_t nch = audio.numChannels;
    const std::size_t n = audio.numFrames;
    hasProcessed_ = true;

    // Input metering and the time-aligned dry path (always fed so that engaging
    // bypass mid-stream is seamless and sample-accurate).
    for (std::size_t c = 0; c < nch; ++c) {
        float peak = 0.0f;
        float* dry = dry_.data() + c * config_.maxBlockFrames;
        for (std::size_t i = 0; i < n; ++i) {
            const float x = audio.channels[c][i];
            peak = std::max(peak, std::fabs(x));
            dry[i] = bypassDelay_[c].processSample(x);
        }
        inPeak_[c].store(peak, std::memory_order_relaxed);
    }

    input_.process(audio);
    eq_.process(audio);
    if (tracking_) {
        tracker_.process(ConstAudioView{audio.channels, nch, n});
        trackCounter_ += n;
        if (trackCounter_ >= trackInterval_) {
            trackCounter_ = 0;
            const LevelAnalysis r = tracker_.result();
            if (r.hasSignal && r.activeSeconds >= kMinTrackedSeconds) {
                trackedLevelDb_ = r.speechLevelDbfs;
                trackedLevelAtomic_.store(static_cast<float>(r.speechLevelDbfs), std::memory_order_relaxed);
                pushSpeechLevel();
            }
        }
    }
    expander_.process(audio);
    deesser_.process(audio);
    comp1_.process(audio);
    comp2_.process(audio);
    presence_.process(audio);

    // Output trim + bypass crossfade.
    const bool crossfade = !bypassRamp_.isFullyOn();
    for (std::size_t i = 0; i < n; ++i) {
        const float g = static_cast<float>(outputGain_.next());
        const float mix = static_cast<float>(bypassRamp_.next());
        for (std::size_t c = 0; c < nch; ++c) {
            const float wet = audio.channels[c][i] * g;
            if (crossfade) {
                const float dry = dry_[c * config_.maxBlockFrames + i];
                audio.channels[c][i] = dry + mix * (wet - dry);
            } else {
                audio.channels[c][i] = wet;
            }
        }
    }

    for (std::size_t c = 0; c < nch; ++c) {
        float peak = 0.0f;
        for (std::size_t i = 0; i < n; ++i)
            peak = std::max(peak, std::fabs(audio.channels[c][i]));
        outPeak_[c].store(peak, std::memory_order_relaxed);
    }
    expGr_.store(expander_.lastGainReductionDb(), std::memory_order_relaxed);
    dsGr_.store(deesser_.lastGainReductionDb(), std::memory_order_relaxed);
    c1Gr_.store(comp1_.lastGainReductionDb(), std::memory_order_relaxed);
    c2Gr_.store(comp2_.lastGainReductionDb(), std::memory_order_relaxed);
}

} // namespace radiff::core

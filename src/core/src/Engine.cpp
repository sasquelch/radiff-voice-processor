#include "radiff/core/Engine.h"

#include <cmath>

namespace radiff::core {

Engine::Engine() { updateDerived(); }

void Engine::prepare(const EngineConfig& config) {
    config_ = config;
    prepared_ = true;
    reset();
}

void Engine::release() noexcept { prepared_ = false; }

void Engine::reset() noexcept {
    // Phase 2: clear filter/detector state here.
}

void Engine::setParameters(const ParameterSet& params) noexcept {
    params_ = params;
    updateDerived();
}

float Engine::dbToGain(float db) noexcept { return std::pow(10.0f, db / 20.0f); }

void Engine::updateDerived() noexcept {
    inputGain_ = dbToGain(params_.get(ParamId::InputGainDb));
    outputGain_ = dbToGain(params_.get(ParamId::OutputGainDb));
}

void Engine::process(AudioView audio) noexcept {
    if (!prepared_ || params_.get(ParamId::Bypass) >= 0.5f)
        return;

    const float gain = inputGain_ * outputGain_;
    for (std::size_t c = 0; c < audio.numChannels; ++c) {
        float* samples = audio.channels[c];
        for (std::size_t i = 0; i < audio.numFrames; ++i)
            samples[i] *= gain;
    }
}

} // namespace radiff::core

#include "radiff/core/modules/Equalizer.h"

#include <algorithm>

namespace radiff::core::modules {

dsp::BiquadCoeffs Equalizer::designBand(double fs, EqBandType type, double hz, double gainDb, double q) noexcept {
    switch (type) {
    case EqBandType::Bell: return dsp::design::peaking(fs, hz, gainDb, q);
    case EqBandType::LowShelf: return dsp::design::lowShelf(fs, hz, gainDb, q);
    case EqBandType::HighShelf: return dsp::design::highShelf(fs, hz, gainDb, q);
    case EqBandType::HighPass: return dsp::design::highPass(fs, hz, q);
    case EqBandType::LowPass: return dsp::design::lowPass(fs, hz, q);
    }
    return dsp::design::identity();
}

void Equalizer::prepare(double sampleRate, std::size_t numChannels) {
    sampleRate_ = sampleRate;
    numChannels_ = std::min(numChannels, kMaxChannels);
    moduleRamp_.prepare(sampleRate, 10.0);
    for (auto& r : bandRamp_)
        r.prepare(sampleRate, 10.0);
    for (auto& ch : filters_)
        for (auto& f : ch)
            f.prepare(sampleRate);
    firstUpdate_ = true;
    reset();
}

void Equalizer::reset() noexcept {
    moduleRamp_.reset(enabled_);
    for (int b = 0; b < kNumEqBands; ++b)
        bandRamp_[static_cast<std::size_t>(b)].reset(bandEnabled_[static_cast<std::size_t>(b)]);
    for (auto& ch : filters_)
        for (auto& f : ch)
            f.reset();
}

void Equalizer::update(const ParameterSet& p) noexcept {
    const bool on = p.getBool(ParamId::EqEnabled);
    for (int b = 0; b < kNumEqBands; ++b) {
        const auto bi = static_cast<std::size_t>(b);
        const bool bandOn = p.getBool(ParameterSet::eqParam(b, "enabled"));
        const auto type = static_cast<EqBandType>(p.getInt(ParameterSet::eqParam(b, "type")));
        const auto coeffs = designBand(sampleRate_, type, static_cast<double>(p.get(ParameterSet::eqParam(b, "frequencyHz"))),
                                       static_cast<double>(p.get(ParameterSet::eqParam(b, "gainDb"))),
                                       static_cast<double>(p.get(ParameterSet::eqParam(b, "q"))));
        for (std::size_t c = 0; c < kMaxChannels; ++c)
            filters_[c][bi].setCoefficients(coeffs, firstUpdate_);
        if (firstUpdate_) {
            bandRamp_[bi].reset(bandOn);
        } else {
            if (bandOn && !bandEnabled_[bi])
                for (auto& ch : filters_)
                    ch[bi].reset();
            bandRamp_[bi].setTarget(bandOn);
        }
        bandEnabled_[bi] = bandOn;
    }
    if (firstUpdate_) {
        moduleRamp_.reset(on);
        firstUpdate_ = false;
    } else {
        if (on && !enabled_)
            for (auto& ch : filters_)
                for (auto& f : ch)
                    f.reset();
        moduleRamp_.setTarget(on);
    }
    enabled_ = on;
}

void Equalizer::process(AudioView audio) noexcept {
    if (!moduleRamp_.isActive())
        return;
    const std::size_t ch = std::min(audio.numChannels, numChannels_);
    std::array<bool, kNumEqBands> active{};
    bool any = false;
    for (std::size_t b = 0; b < kNumEqBands; ++b) {
        active[b] = bandRamp_[b].isActive();
        any = any || active[b];
    }
    if (!any) {
        // Keep the module ramp moving even if no bands are active.
        for (std::size_t i = 0; i < audio.numFrames; ++i)
            (void)moduleRamp_.next();
        return;
    }
    for (std::size_t i = 0; i < audio.numFrames; ++i) {
        const double modMix = moduleRamp_.next();
        std::array<double, kNumEqBands> bandMix{};
        for (std::size_t b = 0; b < kNumEqBands; ++b)
            bandMix[b] = active[b] ? bandRamp_[b].next() : 0.0;
        for (std::size_t c = 0; c < ch; ++c) {
            const double dry = static_cast<double>(audio.channels[c][i]);
            double x = dry;
            for (std::size_t b = 0; b < kNumEqBands; ++b) {
                if (!active[b])
                    continue;
                const double y = filters_[c][b].processSample(x);
                x += bandMix[b] * (y - x);
            }
            audio.channels[c][i] = static_cast<float>(dry + modMix * (x - dry));
        }
    }
}

double Equalizer::magnitudeDb(double hz) const noexcept {
    if (!enabled_)
        return 0.0;
    double db = 0.0;
    for (std::size_t b = 0; b < kNumEqBands; ++b)
        if (bandEnabled_[b])
            db += filters_[0][b].coefficients().magnitudeDb(hz, sampleRate_);
    return db;
}

} // namespace radiff::core::modules

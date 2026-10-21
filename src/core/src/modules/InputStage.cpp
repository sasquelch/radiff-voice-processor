#include "radiff/core/modules/InputStage.h"

#include <algorithm>

namespace radiff::core::modules {

void InputStage::prepare(double sampleRate, std::size_t numChannels) {
    sampleRate_ = sampleRate;
    numChannels_ = std::min(numChannels, kMaxChannels);
    gain_.prepare(sampleRate, 10.0);
    dcRamp_.prepare(sampleRate, 10.0);
    hpfRamp_.prepare(sampleRate, 10.0);
    for (std::size_t c = 0; c < kMaxChannels; ++c) {
        dc_[c].prepare(sampleRate, 5.0);
        for (auto& b : hpf_[c])
            b.prepare(sampleRate);
    }
    firstUpdate_ = true;
    reset();
}

void InputStage::reset() noexcept {
    gain_.reset(gain_.target());
    dcRamp_.reset(dcEnabled_);
    hpfRamp_.reset(hpfEnabled_);
    for (std::size_t c = 0; c < kMaxChannels; ++c) {
        dc_[c].reset();
        for (auto& b : hpf_[c])
            b.reset();
    }
}

void InputStage::update(const ParameterSet& p) noexcept {
    gain_.setTarget(dsp::dbToGain(static_cast<double>(p.get(ParamId::InputGainDb))));

    const bool dcOn = p.getBool(ParamId::InputDcBlockEnabled);
    const bool hpfOn = p.getBool(ParamId::InputHighPassEnabled);
    const int stages = p.getInt(ParamId::InputHighPassSlopeDbOct) >= 24 ? 2 : 1;
    const double hz = static_cast<double>(p.get(ParamId::InputHighPassHz));

    // Butterworth Q values: 2nd order -> 0.7071; 4th order -> 0.5412 and 1.3066.
    const double q1 = stages == 2 ? 0.54119610 : 0.70710678;
    const double q2 = 1.30656296;
    for (std::size_t c = 0; c < kMaxChannels; ++c) {
        hpf_[c][0].setCoefficients(dsp::design::highPass(sampleRate_, hz, q1), firstUpdate_);
        hpf_[c][1].setCoefficients(stages == 2 ? dsp::design::highPass(sampleRate_, hz, q2) : dsp::design::identity(),
                                   firstUpdate_);
    }
    if (firstUpdate_) {
        dcEnabled_ = dcOn;
        hpfEnabled_ = hpfOn;
        dcRamp_.reset(dcOn);
        hpfRamp_.reset(hpfOn);
        gain_.reset(gain_.target());
        firstUpdate_ = false;
    } else {
        if (dcOn && !dcEnabled_)
            for (auto& d : dc_)
                d.reset();
        if (hpfOn && !hpfEnabled_)
            for (auto& ch : hpf_)
                for (auto& b : ch)
                    b.reset();
        dcEnabled_ = dcOn;
        hpfEnabled_ = hpfOn;
        dcRamp_.setTarget(dcOn);
        hpfRamp_.setTarget(hpfOn);
    }
    hpfStages_ = stages;
}

void InputStage::process(AudioView audio) noexcept {
    const std::size_t ch = std::min(audio.numChannels, numChannels_);
    const bool dcActive = dcRamp_.isActive();
    const bool hpfActive = hpfRamp_.isActive();
    for (std::size_t i = 0; i < audio.numFrames; ++i) {
        const float g = static_cast<float>(gain_.next());
        const double dcMix = dcActive ? dcRamp_.next() : 0.0;
        const double hpfMix = hpfActive ? hpfRamp_.next() : 0.0;
        for (std::size_t c = 0; c < ch; ++c) {
            double x = static_cast<double>(audio.channels[c][i] * g);
            if (dcActive) {
                const double y = dc_[c].processSample(x);
                x += dcMix * (y - x);
            }
            if (hpfActive) {
                double y = hpf_[c][0].processSample(x);
                if (hpfStages_ == 2)
                    y = hpf_[c][1].processSample(y);
                x += hpfMix * (y - x);
            }
            audio.channels[c][i] = static_cast<float>(x);
        }
    }
}

double InputStage::magnitudeDb(double hz) const noexcept {
    double db = dsp::gainToDb(gain_.target());
    if (hpfEnabled_) {
        db += hpf_[0][0].coefficients().magnitudeDb(hz, sampleRate_);
        if (hpfStages_ == 2)
            db += hpf_[0][1].coefficients().magnitudeDb(hz, sampleRate_);
    }
    return db;
}

} // namespace radiff::core::modules

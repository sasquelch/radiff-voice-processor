#include "radiff/core/modules/Compressor.h"
#include "radiff/core/dsp/GainComputer.h"

#include <algorithm>
#include <cmath>

namespace radiff::core::modules {

Compressor::Compressor(Module which) noexcept {
    auto id = [&](std::string_view key) { return *paramIdFor(which, -1, key); };
    ids_ = {id("enabled"),     id("threshold" "Dbfs"), id("ratio"),      id("kneeDb"),
            id("attackMs"),    id("releaseMs"),        id("makeupGainDb"), id("detector"),
            id("rmsWindowMs"), id("sidechainHighPassEnabled"), id("sidechainHighPassHz"), id("autoRelease"),
            id("wet"),         id("stereoLink")};
}

void Compressor::prepare(double sampleRate, std::size_t numChannels) {
    sampleRate_ = sampleRate;
    numChannels_ = std::min(numChannels, kMaxChannels);
    for (auto& s : ch_) {
        s.detector.prepare(sampleRate);
        s.scHp.prepare(sampleRate);
        s.ballistics.prepare(sampleRate);
    }
    makeup_.prepare(sampleRate, 10.0);
    wet_.prepare(sampleRate, 10.0);
    longTermCoef_ = dsp::timeConstantCoefficient(1000.0, sampleRate);
    firstUpdate_ = true;
    reset();
}

void Compressor::reset() noexcept {
    for (auto& s : ch_) {
        s.detector.reset();
        s.scHp.reset();
        s.ballistics.reset();
    }
    makeup_.reset(makeup_.target());
    wet_.reset(wet_.target());
    longTermGr_ = 0.0;
    releaseMultiplier_ = 1.0;
    lastGrDb_ = 0.0f;
}

void Compressor::update(const ParameterSet& p) noexcept {
    enabled_ = p.getBool(ids_.enabled);
    thresholdDb_ = static_cast<double>(p.get(ids_.threshold));
    ratio_ = static_cast<double>(p.get(ids_.ratio));
    kneeDb_ = static_cast<double>(p.get(ids_.knee));
    link_ = p.getBool(ids_.stereoLink);
    scHpOn_ = p.getBool(ids_.scHpEnabled);
    autoRelease_ = p.getBool(ids_.autoRelease);
    releaseMs_ = static_cast<double>(p.get(ids_.release));
    makeup_.setTarget(dsp::dbToGain(static_cast<double>(p.get(ids_.makeup))));
    wet_.setTarget(static_cast<double>(p.get(ids_.wet)));

    const auto mode = p.getInt(ids_.detector) == static_cast<int>(DetectorType::Peak) ? dsp::LevelDetector::Mode::Peak
                                                                                      : dsp::LevelDetector::Mode::Rms;
    const auto hp = dsp::design::highPass(sampleRate_, static_cast<double>(p.get(ids_.scHpHz)), 0.7071);
    for (auto& s : ch_) {
        s.detector.setMode(mode);
        s.detector.setWindow(static_cast<double>(p.get(ids_.rmsWindow)));
        s.ballistics.setTimes(static_cast<double>(p.get(ids_.attack)), releaseMs_);
        s.scHp.setCoefficients(hp, firstUpdate_);
    }
    releaseCoef_ = ch_[0].ballistics.releaseCoefficient();
    if (firstUpdate_) {
        makeup_.reset(makeup_.target());
        wet_.reset(wet_.target());
        firstUpdate_ = false;
    }
}

double Compressor::staticGainDb(double levelDb) const noexcept {
    return dsp::compressorGainDb(levelDb, thresholdDb_, ratio_, kneeDb_);
}

inline double Compressor::smoothedGain(ChannelState& s, double targetDb) noexcept {
    double relCoef = -1.0;
    if (autoRelease_) {
        // Long-term average of the applied reduction drives the release stretch (1x..4x).
        const double gr = -s.ballistics.gainDb();
        longTermGr_ = gr + longTermCoef_ * (longTermGr_ - gr);
        releaseMultiplier_ = 1.0 + 3.0 * std::clamp(longTermGr_ / 6.0, 0.0, 1.0);
        relCoef = dsp::timeConstantCoefficient(releaseMs_ * releaseMultiplier_, sampleRate_);
    }
    return s.ballistics.towardsCompressor(targetDb, relCoef);
}

void Compressor::process(AudioView audio) noexcept {
    const std::size_t nch = std::min(audio.numChannels, numChannels_);
    float maxGr = 0.0f;
    std::array<double, kMaxChannels> level{};
    for (std::size_t i = 0; i < audio.numFrames; ++i) {
        const double wet = wet_.next();
        const double makeup = makeup_.next();
        for (std::size_t c = 0; c < nch; ++c) {
            double d = static_cast<double>(audio.channels[c][i]);
            if (scHpOn_)
                d = ch_[c].scHp.processSample(d);
            level[c] = ch_[c].detector.processSample(d);
        }
        std::array<double, kMaxChannels> gainDb{};
        if (!enabled_) {
            for (std::size_t c = 0; c < nch; ++c)
                gainDb[c] = ch_[c].ballistics.towardsCompressor(0.0);
        } else if (link_ && nch > 1) {
            const double l = std::max(level[0], level[1]);
            const double g = smoothedGain(ch_[0], dsp::compressorGainDb(l, thresholdDb_, ratio_, kneeDb_));
            gainDb[0] = gainDb[1] = g;
        } else {
            for (std::size_t c = 0; c < nch; ++c)
                gainDb[c] = smoothedGain(ch_[c], dsp::compressorGainDb(level[c], thresholdDb_, ratio_, kneeDb_));
        }
        const double mk = enabled_ ? makeup : 1.0;
        for (std::size_t c = 0; c < nch; ++c) {
            const double dry = static_cast<double>(audio.channels[c][i]);
            const double proc = dry * dsp::dbToGain(gainDb[c]) * mk;
            audio.channels[c][i] = static_cast<float>(dry + wet * (proc - dry));
            maxGr = std::max(maxGr, static_cast<float>(-gainDb[c]));
        }
    }
    lastGrDb_ = maxGr;
}

} // namespace radiff::core::modules

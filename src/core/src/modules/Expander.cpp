#include "radiff/core/modules/Expander.h"
#include "radiff/core/dsp/GainComputer.h"

#include <algorithm>
#include <cmath>

namespace radiff::core::modules {

void Expander::prepare(double sampleRate, std::size_t numChannels) {
    sampleRate_ = sampleRate;
    numChannels_ = std::min(numChannels, kMaxChannels);
    const std::size_t maxDelay = dsp::msToSamples(kMaxLookaheadMs, sampleRate) + 1;
    for (auto& s : ch_) {
        s.detector.prepare(sampleRate);
        s.scHp.prepare(sampleRate);
        s.scLp.prepare(sampleRate);
        s.ballistics.prepare(sampleRate);
        s.delay.prepare(maxDelay);
    }
    wet_.prepare(sampleRate, 10.0);
    firstUpdate_ = true;
    reset();
}

void Expander::reset() noexcept {
    for (auto& s : ch_) {
        s.detector.reset();
        s.scHp.reset();
        s.scLp.reset();
        s.ballistics.reset();
        s.delay.reset();
        s.open = true;
        s.holdRemaining = 0;
    }
    wet_.reset(wet_.target());
    lastGrDb_ = 0.0f;
}

void Expander::update(const ParameterSet& p) noexcept {
    enabled_ = p.getBool(ParamId::ExpEnabled);
    thresholdDb_ = static_cast<double>(p.get(ParamId::ExpThresholdDbfs));
    ratio_ = static_cast<double>(p.get(ParamId::ExpRatio));
    kneeDb_ = static_cast<double>(p.get(ParamId::ExpKneeDb));
    hysteresisDb_ = static_cast<double>(p.get(ParamId::ExpHysteresisDb));
    maxAttenDb_ = static_cast<double>(p.get(ParamId::ExpMaxAttenuationDb));
    holdSamples_ = dsp::msToSamples(static_cast<double>(p.get(ParamId::ExpHoldMs)), sampleRate_);
    lookaheadSamples_ = dsp::msToSamples(static_cast<double>(p.get(ParamId::ExpLookaheadMs)), sampleRate_);
    link_ = p.getBool(ParamId::ExpStereoLink);
    scHpOn_ = p.getBool(ParamId::ExpSidechainHighPassEnabled);
    scLpOn_ = p.getBool(ParamId::ExpSidechainLowPassEnabled);
    wet_.setTarget(static_cast<double>(p.get(ParamId::ExpWet)));

    const auto mode = p.getInt(ParamId::ExpDetector) == static_cast<int>(DetectorType::Peak)
                          ? dsp::LevelDetector::Mode::Peak
                          : dsp::LevelDetector::Mode::Rms;
    const auto hp = dsp::design::highPass(sampleRate_, static_cast<double>(p.get(ParamId::ExpSidechainHighPassHz)), 0.7071);
    const auto lp = dsp::design::lowPass(sampleRate_, static_cast<double>(p.get(ParamId::ExpSidechainLowPassHz)), 0.7071);
    for (auto& s : ch_) {
        s.detector.setMode(mode);
        s.detector.setWindow(static_cast<double>(p.get(ParamId::ExpRmsWindowMs)));
        s.ballistics.setTimes(static_cast<double>(p.get(ParamId::ExpAttackMs)),
                              static_cast<double>(p.get(ParamId::ExpReleaseMs)));
        s.scHp.setCoefficients(hp, firstUpdate_);
        s.scLp.setCoefficients(lp, firstUpdate_);
        s.delay.setDelay(lookaheadSamples_);
    }
    if (firstUpdate_) {
        wet_.reset(wet_.target());
        firstUpdate_ = false;
    }
}

double Expander::staticGainDb(double levelDb) const noexcept {
    return dsp::expanderGainDb(levelDb, thresholdDb_, ratio_, kneeDb_, maxAttenDb_);
}

inline double Expander::targetGainFor(ChannelState& s, double levelDb) noexcept {
    // State machine: opens when the level reaches the threshold, closes only once the level
    // has fallen below (threshold - hysteresis). While open, the static curve is evaluated
    // with the threshold shifted down by the hysteresis, so the gain stays near unity until
    // the close point; the soft knee therefore remains continuous when hysteresis is zero.
    if (levelDb >= thresholdDb_) {
        s.open = true;
        s.holdRemaining = holdSamples_;
    } else if (levelDb < thresholdDb_ - hysteresisDb_) {
        s.open = false;
    }
    if (s.open)
        return dsp::expanderGainDb(levelDb, thresholdDb_ - hysteresisDb_, ratio_, kneeDb_, maxAttenDb_);
    if (s.holdRemaining > 0) {
        --s.holdRemaining; // hold: stay fully open after speech stops
        return 0.0;
    }
    return dsp::expanderGainDb(levelDb, thresholdDb_, ratio_, kneeDb_, maxAttenDb_);
}

void Expander::process(AudioView audio) noexcept {
    const std::size_t nch = std::min(audio.numChannels, numChannels_);
    float maxGr = 0.0f;
    std::array<double, kMaxChannels> level{};

    for (std::size_t i = 0; i < audio.numFrames; ++i) {
        const double wet = wet_.next();
        // 1. Detector on the undelayed input.
        for (std::size_t c = 0; c < nch; ++c) {
            double d = static_cast<double>(audio.channels[c][i]);
            if (scHpOn_)
                d = ch_[c].scHp.processSample(d);
            if (scLpOn_)
                d = ch_[c].scLp.processSample(d);
            level[c] = ch_[c].detector.processSample(d);
        }
        // 2. Gain computation (linked: one state machine on the louder channel).
        std::array<double, kMaxChannels> gainDb{};
        if (!enabled_) {
            for (std::size_t c = 0; c < nch; ++c)
                gainDb[c] = ch_[c].ballistics.towardsExpander(0.0);
        } else if (link_ && nch > 1) {
            const double l = std::max(level[0], level[1]);
            const double target = targetGainFor(ch_[0], l);
            const double g = ch_[0].ballistics.towardsExpander(target);
            gainDb[0] = gainDb[1] = g;
        } else {
            for (std::size_t c = 0; c < nch; ++c)
                gainDb[c] = ch_[c].ballistics.towardsExpander(targetGainFor(ch_[c], level[c]));
        }
        // 3. Apply to the delayed audio, then wet/dry.
        for (std::size_t c = 0; c < nch; ++c) {
            const double dry = static_cast<double>(ch_[c].delay.processSample(audio.channels[c][i]));
            const double g = dsp::dbToGain(gainDb[c]);
            const double out = dry * (1.0 - wet) + dry * g * wet;
            audio.channels[c][i] = static_cast<float>(out);
            maxGr = std::max(maxGr, static_cast<float>(-gainDb[c]));
        }
    }
    lastGrDb_ = maxGr;
}

} // namespace radiff::core::modules

#include "radiff/core/modules/Expander.h"
#include "radiff/core/dsp/GainComputer.h"

#include <algorithm>
#include <cmath>

namespace radiff::core::modules {

void Expander::prepare(double sampleRate, std::size_t numChannels) {
    sampleRate_ = sampleRate;
    numChannels_ = std::min(numChannels, kMaxChannels);
    const std::size_t maxDelay = dsp::msToSamples(kMaxLookaheadMs, sampleRate) + 1;
    freezeSamples_ = dsp::msToSamples(kFreezeMs, sampleRate);
    trackCoef_ = 1.0 - dsp::timeConstantCoefficient(kTrackMs, sampleRate);
    clickPre_ = dsp::msToSamples(kClickPreMs, sampleRate);
    clickPost_ = dsp::msToSamples(kClickPostMs, sampleRate);
    clickRamp_ = std::max<std::size_t>(1, dsp::msToSamples(kClickRampMs, sampleRate));
    clickEval_ = std::max<std::size_t>(1, dsp::msToSamples(kClickEvalMs, sampleRate));
    clickMax_ = dsp::msToSamples(kClickMaxMs, sampleRate);
    for (auto& s : ch_) {
        s.detector.prepare(sampleRate);
        s.fastDetector.prepare(sampleRate);
        s.fastDetector.setMode(dsp::LevelDetector::Mode::Rms);
        s.fastDetector.setWindow(kFastWindowMs);
        s.scHp.prepare(sampleRate);
        s.scLp.prepare(sampleRate);
        s.ballistics.prepare(sampleRate);
        s.delay.prepare(maxDelay);
        s.aboveRing.assign(maxDelay, 0);
    }
    wet_.prepare(sampleRate, 10.0);
    firstUpdate_ = true;
    reset();
}

void Expander::reset() noexcept {
    for (auto& s : ch_) {
        s.detector.reset();
        s.fastDetector.reset();
        s.scHp.reset();
        s.scLp.reset();
        s.ballistics.reset();
        s.delay.reset();
        s.open = true;
        s.holdRemaining = 0;
        s.openSamples = 0;
        std::fill(s.aboveRing.begin(), s.aboveRing.end(), std::uint8_t{0});
        s.ringPos = 0;
        s.aboveCount = 0;
        s.lastAbove = false;
        s.freezeRemaining = 0;
        s.closedLevelDb = -144.0;
        s.t = 0;
        s.fastPrev = -200.0;
        s.evaluating = false;
        s.clickStart = s.clickEnd = 0;
        s.evalRemaining = 0;
        s.preLevelDb = s.clickPeakDb = -200.0;
        s.postEnergy = 0.0;
        s.postCount = 0;
        for (auto& d : s.dips)
            d = ChannelState::Dip{};
    }
    wet_.reset(wet_.target());
    lastGrDb_ = 0.0f;
}

void Expander::setSpeechLevelDb(std::optional<double> levelDb) noexcept {
    speechLevelDb_ = levelDb;
    resolveThreshold();
}

void Expander::resolveThreshold() noexcept {
    if (thresholdMode_ == ThresholdMode::SpeechRelative && speechLevelDb_.has_value())
        thresholdDb_ = std::clamp(*speechLevelDb_ + relativeThresholdDb_, -80.0, 0.0);
    else
        thresholdDb_ = absoluteThresholdDb_;
}

void Expander::update(const ParameterSet& p) noexcept {
    enabled_ = p.getBool(ParamId::ExpEnabled);
    absoluteThresholdDb_ = static_cast<double>(p.get(ParamId::ExpThresholdDbfs));
    relativeThresholdDb_ = static_cast<double>(p.get(ParamId::ExpRelativeThresholdDb));
    thresholdMode_ = p.getInt(ParamId::ExpThresholdMode) == static_cast<int>(ThresholdMode::SpeechRelative)
                         ? ThresholdMode::SpeechRelative
                         : ThresholdMode::Absolute;
    resolveThreshold();
    ratio_ = static_cast<double>(p.get(ParamId::ExpRatio));
    kneeDb_ = static_cast<double>(p.get(ParamId::ExpKneeDb));
    hysteresisDb_ = static_cast<double>(p.get(ParamId::ExpHysteresisDb));
    maxAttenDb_ = static_cast<double>(p.get(ParamId::ExpMaxAttenuationDb));
    holdSamples_ = dsp::msToSamples(static_cast<double>(p.get(ParamId::ExpHoldMs)), sampleRate_);
    lookaheadSamples_ = dsp::msToSamples(static_cast<double>(p.get(ParamId::ExpLookaheadMs)), sampleRate_);
    const std::size_t reject = dsp::msToSamples(static_cast<double>(p.get(ParamId::ExpTransientRejectMs)), sampleRate_);
    if (reject != rejectSamples_) {
        rejectSamples_ = std::min(reject, ch_[0].aboveRing.size());
        verifySamples_ = std::max<std::size_t>(1, (rejectSamples_ * 2 + 2) / 3); // two thirds, rounded up
        for (auto& s : ch_) { // window length changed: restart the verification history
            std::fill(s.aboveRing.begin(), s.aboveRing.end(), std::uint8_t{0});
            s.ringPos = 0;
            s.aboveCount = 0;
            s.lastAbove = false;
        }
    }
    clickDuckDb_ = static_cast<double>(p.get(ParamId::ExpClickDuckDb));
    delaySamples_ = std::max(lookaheadSamples_, rejectSamples_);
    if (clickDuckDb_ > 0.0)
        delaySamples_ = std::max(delaySamples_, std::min(dsp::msToSamples(kClickMinDelayMs, sampleRate_),
                                                         ch_[0].aboveRing.size() - 1));
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
        s.delay.setDelay(delaySamples_);
    }
    if (firstUpdate_) {
        wet_.reset(wet_.target());
        firstUpdate_ = false;
    }
}

double Expander::staticGainDb(double levelDb) const noexcept {
    return dsp::expanderGainDb(levelDb, thresholdDb_, ratio_, kneeDb_, maxAttenDb_);
}

inline double Expander::targetGainFor(ChannelState& s, double levelDb, double fastDb) noexcept {
    // State machine. Open -> closed once the level has fallen below (threshold - hysteresis).
    // Closed -> open when the level reaches the threshold; with transient rejection the fast
    // detector must have been there for two thirds of the last `rejectSamples_` first. While open,
    // the static curve is evaluated with the threshold shifted down by the hysteresis, so the
    // gain stays near unity until the close point; the soft knee therefore remains continuous
    // when hysteresis is zero.
    if (s.open) {
        ++s.openSamples;
        if (levelDb >= thresholdDb_) {
            s.holdRemaining = holdSamples_;
        } else if (levelDb < thresholdDb_ - hysteresisDb_) {
            s.open = false;
            if (rejectSamples_ > 0 && s.openSamples <= rejectSamples_) {
                // Open for no longer than the verification window: that was a transient after
                // all. Keep the pre-burst level and let it ring down like a rejected burst.
                s.freezeRemaining = freezeSamples_;
            } else {
                s.freezeRemaining = 0;
                s.closedLevelDb = levelDb;
            }
        }
    }
    bool burst = false;
    if (!s.open) {
        if (rejectSamples_ == 0) {
            if (levelDb >= thresholdDb_) {
                s.open = true;
                s.openSamples = 0;
                s.holdRemaining = holdSamples_;
            }
        } else {
            // Flag with a little hysteresis so low-frequency ripple on the fast detector does
            // not fragment a genuine onset.
            const bool above = fastDb >= thresholdDb_ || (s.lastAbove && fastDb >= thresholdDb_ - kVerifyHysteresisDb);
            s.lastAbove = above;
            s.aboveCount += static_cast<std::size_t>(above) - s.aboveRing[s.ringPos];
            s.aboveRing[s.ringPos] = static_cast<std::uint8_t>(above);
            if (++s.ringPos >= rejectSamples_)
                s.ringPos = 0;
            if (s.aboveCount >= verifySamples_ && levelDb >= thresholdDb_) { // verified onset
                s.open = true;
                s.openSamples = 0;
                s.holdRemaining = holdSamples_;
                s.aboveCount = 0;
                std::fill_n(s.aboveRing.begin(), rejectSamples_, std::uint8_t{0});
                s.ringPos = 0;
                s.lastAbove = false;
            } else if (s.aboveCount > 0) {
                burst = true; // an unverified burst is in (or has just left) the window
                s.freezeRemaining = freezeSamples_;
            } else {
                if (s.freezeRemaining > 0) {
                    // Keep the pre-burst level until the main detector has rung down to it.
                    if (levelDb <= s.closedLevelDb + 3.0)
                        s.freezeRemaining = 0;
                    else
                        --s.freezeRemaining;
                }
                if (s.freezeRemaining == 0) {
                    // Pre-burst level tracker: follows the level down instantly but up only
                    // slowly (20 ms), so the first samples of a burst cannot drag it above the
                    // threshold.
                    if (levelDb < s.closedLevelDb)
                        s.closedLevelDb = levelDb;
                    else
                        s.closedLevelDb += trackCoef_ * (levelDb - s.closedLevelDb);
                }
            }
        }
    }
    if (s.open)
        return dsp::expanderGainDb(levelDb, thresholdDb_ - hysteresisDb_, ratio_, kneeDb_, maxAttenDb_);
    if (s.holdRemaining > 0) {
        --s.holdRemaining; // hold: stay fully open after speech stops
        return 0.0;
    }
    // Closed. During an unverified burst (and while it rings through the detector) the
    // attenuation of the preceding level is kept instead of letting the curve return to unity.
    const double l = (burst || s.freezeRemaining > 0) ? s.closedLevelDb : levelDb;
    return dsp::expanderGainDb(l, thresholdDb_, ratio_, kneeDb_, maxAttenDb_);
}

inline double Expander::clickDuckFor(ChannelState& s, double peakAbs, double fastDb, double levelDb) noexcept {
    ++s.t;
    if (clickDuckDb_ <= 0.0)
        return 0.0;
    const double peakDb = dsp::gainToDb(peakAbs);
    // 1. Detection / evaluation on the input time line.
    if (!s.evaluating) {
        if (peakDb >= s.fastPrev + kClickCrestDb && peakDb >= thresholdDb_ - kClickMinBelowDb && levelDb < thresholdDb_) {
            s.evaluating = true;
            s.clickStart = s.clickEnd = s.t;
            s.evalRemaining = clickEval_;
            s.preLevelDb = s.fastPrev;
            s.clickPeakDb = peakDb;
            s.postEnergy = 0.0;
            s.postCount = 0;
        }
    } else {
        if (peakDb >= s.preLevelDb + kClickCrestDb) { // still part of the peaky stretch
            s.clickEnd = s.t;
            s.clickPeakDb = std::max(s.clickPeakDb, peakDb);
            s.postEnergy = 0.0;
            s.postCount = 0;
        } else {
            s.postEnergy += peakAbs * peakAbs;
            ++s.postCount;
        }
        if (s.clickEnd - s.clickStart > clickMax_) {
            s.evaluating = false; // a consonant or an onset, not a click
        } else if (--s.evalRemaining == 0) {
            s.evaluating = false;
            // What follows the peaky stretch must be as quiet as what preceded it (or, over
            // digital silence, at least 30 dB under the click's own peak): an isolated click.
            // Anything sustained or followed by voice fails and passes untouched.
            const double postDb = s.postCount > 0 ? dsp::powerToDb(s.postEnergy / static_cast<double>(s.postCount)) : -200.0;
            if (s.postCount > 0 && postDb <= std::max(s.preLevelDb + 6.0, s.clickPeakDb - 30.0)) {
                for (auto& d : s.dips) {
                    if (!d.active) {
                        d.a = s.clickStart > clickPre_ ? s.clickStart - clickPre_ : 0;
                        d.b = s.clickEnd + clickPost_;
                        d.active = true;
                        break;
                    }
                }
            }
        }
    }
    s.fastPrev = fastDb;
    // 2. Duck on the output time line (the audio leaves delaySamples_ later).
    if (s.t < delaySamples_)
        return 0.0;
    const std::uint64_t to = s.t - delaySamples_;
    double duck = 0.0;
    for (auto& d : s.dips) {
        if (!d.active)
            continue;
        if (to > d.b + clickRamp_) {
            d.active = false;
            continue;
        }
        double env = 0.0;
        if (to + clickRamp_ < d.a)
            continue; // not yet
        if (to < d.a)
            env = static_cast<double>(to + clickRamp_ - d.a) / static_cast<double>(clickRamp_);
        else if (to <= d.b)
            env = 1.0;
        else
            env = 1.0 - static_cast<double>(to - d.b) / static_cast<double>(clickRamp_);
        duck = std::min(duck, -clickDuckDb_ * std::clamp(env, 0.0, 1.0));
    }
    return duck;
}

void Expander::process(AudioView audio) noexcept {
    const std::size_t nch = std::min(audio.numChannels, numChannels_);
    float maxGr = 0.0f;
    std::array<double, kMaxChannels> level{}, fast{}, peak{};
    const bool verify = rejectSamples_ > 0 || clickDuckDb_ > 0.0;

    for (std::size_t i = 0; i < audio.numFrames; ++i) {
        const double wet = wet_.next();
        // 1. Detectors on the undelayed input.
        for (std::size_t c = 0; c < nch; ++c) {
            double d = static_cast<double>(audio.channels[c][i]);
            if (scHpOn_)
                d = ch_[c].scHp.processSample(d);
            if (scLpOn_)
                d = ch_[c].scLp.processSample(d);
            level[c] = ch_[c].detector.processSample(d);
            fast[c] = verify ? ch_[c].fastDetector.processSample(d) : level[c];
            peak[c] = std::fabs(d);
        }
        // 2. Gain computation (linked: one state machine on the louder channel).
        std::array<double, kMaxChannels> gainDb{};
        if (!enabled_) {
            for (std::size_t c = 0; c < nch; ++c)
                gainDb[c] = ch_[c].ballistics.towardsExpander(0.0);
        } else if (link_ && nch > 1) {
            const double l = std::max(level[0], level[1]);
            const double f = std::max(fast[0], fast[1]);
            const double pk = std::max(peak[0], peak[1]);
            const double target = targetGainFor(ch_[0], l, f);
            const double g = std::min(ch_[0].ballistics.towardsExpander(target), clickDuckFor(ch_[0], pk, f, l));
            gainDb[0] = gainDb[1] = g;
        } else {
            for (std::size_t c = 0; c < nch; ++c)
                gainDb[c] = std::min(ch_[c].ballistics.towardsExpander(targetGainFor(ch_[c], level[c], fast[c])),
                                     clickDuckFor(ch_[c], peak[c], fast[c], level[c]));
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

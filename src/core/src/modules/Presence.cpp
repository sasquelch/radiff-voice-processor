#include "radiff/core/modules/Presence.h"
#include "radiff/core/dsp/Units.h"

#include <algorithm>
#include <cmath>

namespace radiff::core::modules {

void Presence::prepare(double sampleRate, std::size_t numChannels) {
    sampleRate_ = sampleRate;
    numChannels_ = std::clamp<std::size_t>(numChannels, 1, kMaxChannels);
    for (auto& s : ch_) {
        s.lf.prepare(sampleRate);
        s.hf.prepare(sampleRate);
        s.detectorHp.prepare(sampleRate);
        s.hfLevel.prepare(sampleRate);
        s.hfLevel.setWindow(kDetectorMs);
        s.wideLevel.prepare(sampleRate);
        s.wideLevel.setWindow(kDetectorMs);
    }
    ramp_.prepare(sampleRate, 10.0);
    makeup_.prepare(sampleRate, 10.0);
    hfGain_.prepare(sampleRate, kGainSmoothMs);
    firstUpdate_ = true;
    reset();
}

void Presence::reset() noexcept {
    for (auto& s : ch_) {
        s.lf.reset();
        s.hf.reset();
        s.detectorHp.reset();
        s.hfLevel.reset();
        s.wideLevel.reset();
    }
    ramp_.reset(enabled_);
    makeup_.reset(makeup_.target());
    hfGain_.reset(hfStaticDb_);
    hfCurrentDb_ = hfStaticDb_;
    designHf(hfStaticDb_, true);
    counter_ = 0;
    idle_ = !enabled_;
}

void Presence::designHf(double gainDb, bool immediate) noexcept {
    const auto c = dsp::design::highShelf(sampleRate_, hfHz_, gainDb, 0.707);
    for (auto& s : ch_)
        s.hf.setCoefficients(c, immediate);
    hfDesignedDb_ = gainDb;
}

void Presence::update(const ParameterSet& p) noexcept {
    enabled_ = p.getBool(ParamId::PresEnabled);
    lfHz_ = static_cast<double>(p.get(ParamId::PresLfFrequencyHz));
    lfGainDb_ = kLfDbPerStep * static_cast<double>(p.get(ParamId::PresLfDetail));
    hfHz_ = static_cast<double>(p.get(ParamId::PresHfFrequencyHz));
    hfStaticDb_ = kHfDbPerStep * static_cast<double>(p.get(ParamId::PresHfDetail));
    dynamic_ = std::clamp(static_cast<double>(p.get(ParamId::PresHfDynamic)), 0.0, 1.0);
    lfCoeffs_ = dsp::design::lowShelf(sampleRate_, lfHz_, lfGainDb_, 0.707);
    hfStaticCoeffs_ = dsp::design::highShelf(sampleRate_, hfHz_, hfStaticDb_, 0.707);
    const auto hp = dsp::design::highPass(sampleRate_, hfHz_, 0.707);
    for (auto& s : ch_) {
        s.lf.setCoefficients(lfCoeffs_, firstUpdate_);
        s.detectorHp.setCoefficients(hp, firstUpdate_);
    }
    makeup_.setTarget(dsp::dbToGain(static_cast<double>(p.get(ParamId::PresMakeupGainDb))));
    ramp_.setTarget(enabled_);
    if (firstUpdate_) {
        makeup_.reset(makeup_.target());
        hfGain_.reset(hfStaticDb_);
        hfCurrentDb_ = hfStaticDb_;
        designHf(hfStaticDb_, true);
        ramp_.reset(enabled_);
        idle_ = !enabled_;
    } else {
        // New static gain or frequency: re-design at the current dynamic gain (ramped).
        designHf(hfCurrentDb_, false);
    }
    firstUpdate_ = false;
}

double Presence::magnitudeDb(double hz) const noexcept {
    return lfCoeffs_.magnitudeDb(hz, sampleRate_) + hfStaticCoeffs_.magnitudeDb(hz, sampleRate_);
}

void Presence::process(AudioView audio) noexcept {
    const std::size_t nch = std::min(audio.numChannels, numChannels_);
    const std::size_t n = audio.numFrames;
    if (!ramp_.isActive()) {
        if (!idle_) {
            for (auto& s : ch_) {
                s.lf.reset();
                s.hf.reset();
                s.detectorHp.reset();
                s.hfLevel.reset();
                s.wideLevel.reset();
            }
            hfGain_.reset(hfStaticDb_);
            hfCurrentDb_ = hfStaticDb_;
            designHf(hfStaticDb_, true);
            idle_ = true;
        }
        return;
    }
    idle_ = false;
    for (std::size_t i = 0; i < n; ++i) {
        // Detection on the module input (louder channel decides).
        double dominance = -200.0;
        for (std::size_t c = 0; c < nch; ++c) {
            const double x = static_cast<double>(audio.channels[c][i]);
            const double hp = ch_[c].detectorHp.processSample(x);
            const double hfDb = ch_[c].hfLevel.processSample(hp);
            const double wideDb = ch_[c].wideLevel.processSample(x);
            dominance = std::max(dominance, hfDb - wideDb);
        }
        if (counter_ == 0) {
            const double r = std::clamp((dominance + kDominanceRangeDb) / kDominanceRangeDb, 0.0, 1.0);
            hfGain_.setTarget(hfStaticDb_ * (1.0 - dynamic_ * r));
        }
        hfCurrentDb_ = hfGain_.next();
        if (counter_ == 0 && std::fabs(hfCurrentDb_ - hfDesignedDb_) > 0.01)
            designHf(hfCurrentDb_, false);
        if (++counter_ >= kUpdateInterval)
            counter_ = 0;

        const double mix = ramp_.next();
        const double g = makeup_.next();
        for (std::size_t c = 0; c < nch; ++c) {
            const double x = static_cast<double>(audio.channels[c][i]);
            const double wet = ch_[c].hf.processSample(ch_[c].lf.processSample(x)) * g;
            audio.channels[c][i] = static_cast<float>(x + mix * (wet - x));
        }
    }
}

} // namespace radiff::core::modules

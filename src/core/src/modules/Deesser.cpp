#include "radiff/core/modules/Deesser.h"
#include "radiff/core/dsp/GainComputer.h"
#include "radiff/core/dsp/Units.h"

#include <algorithm>
#include <cmath>

namespace radiff::core::modules {

void Deesser::prepare(double sampleRate, std::size_t numChannels) {
    sampleRate_ = sampleRate;
    numChannels_ = std::clamp<std::size_t>(numChannels, 1, kMaxChannels);
    for (auto& s : ch_) {
        s.band.prepare(sampleRate);
        s.detector.prepare(sampleRate);
        s.detector.setMode(dsp::LevelDetector::Mode::Peak);
        s.detector.setWindow(kDetectorDecayMs);
        s.ballistics.prepare(sampleRate);
    }
    ramp_.prepare(sampleRate, 10.0);
    firstUpdate_ = true;
    reset();
}

void Deesser::reset() noexcept {
    for (auto& s : ch_) {
        s.band.reset();
        s.detector.reset();
        s.ballistics.reset();
    }
    ramp_.reset(enabled_);
    idle_ = !enabled_;
    lastGrDb_ = 0.0f;
}

void Deesser::update(const ParameterSet& p) noexcept {
    enabled_ = p.getBool(ParamId::DeessEnabled);
    link_ = p.getBool(ParamId::DeessStereoLink);
    listen_ = p.getBool(ParamId::DeessListen);
    thresholdDb_ = static_cast<double>(p.get(ParamId::DeessThresholdDbfs));
    maxReductionDb_ = static_cast<double>(p.get(ParamId::DeessMaxReductionDb));
    const double q = dsp::design::qFromBandwidthOctaves(static_cast<double>(p.get(ParamId::DeessBandwidthOct)));
    bandCoeffs_ = dsp::design::bandPass(sampleRate_, static_cast<double>(p.get(ParamId::DeessFrequencyHz)), q);
    for (auto& s : ch_) {
        s.band.setCoefficients(bandCoeffs_, firstUpdate_);
        s.ballistics.setTimes(static_cast<double>(p.get(ParamId::DeessAttackMs)),
                              static_cast<double>(p.get(ParamId::DeessReleaseMs)));
    }
    ramp_.setTarget(enabled_);
    if (firstUpdate_) {
        ramp_.reset(enabled_);
        idle_ = !enabled_;
    }
    firstUpdate_ = false;
}

double Deesser::staticReductionDb(double bandLevelDb) const noexcept {
    // 1:inf limiting of the band above the threshold, quadratic 6 dB knee, hard cap.
    const double g = dsp::compressorGainDb(bandLevelDb, thresholdDb_, 1.0e6, kKneeDb);
    return std::min(-g, std::max(0.0, maxReductionDb_));
}

void Deesser::process(AudioView audio) noexcept {
    const std::size_t nch = std::min(audio.numChannels, numChannels_);
    const std::size_t n = audio.numFrames;
    if (!ramp_.isActive()) {
        if (!idle_) { // fully faded out: drop detector/filter history so re-enabling starts clean
            for (auto& s : ch_) {
                s.band.reset();
                s.detector.reset();
                s.ballistics.reset();
            }
            idle_ = true;
        }
        lastGrDb_ = 0.0f;
        return;
    }
    idle_ = false;
    float maxGr = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        std::array<double, kMaxChannels> band{}, level{};
        for (std::size_t c = 0; c < nch; ++c) {
            band[c] = ch_[c].band.processSample(static_cast<double>(audio.channels[c][i]));
            level[c] = ch_[c].detector.processSample(band[c]);
        }
        if (link_ && nch == 2) {
            const double m = std::max(level[0], level[1]);
            level[0] = level[1] = m;
        }
        const double mix = ramp_.next();
        for (std::size_t c = 0; c < nch; ++c) {
            const double gDb = ch_[c].ballistics.towardsCompressor(-staticReductionDb(level[c]));
            maxGr = std::max(maxGr, static_cast<float>(-gDb));
            const double g = dsp::dbToGain(gDb);
            const double x = static_cast<double>(audio.channels[c][i]);
            const double wet = listen_ ? band[c] : x - (1.0 - g) * band[c];
            audio.channels[c][i] = static_cast<float>(x + mix * (wet - x));
        }
    }
    lastGrDb_ = maxGr;
}

} // namespace radiff::core::modules

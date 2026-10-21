#include "radiff/core/Analysis.h"
#include "radiff/core/dsp/Units.h"

#include <algorithm>
#include <cmath>

namespace radiff::core {

void LevelAnalyzer::prepare(double sampleRate, std::size_t numChannels) noexcept {
    sampleRate_ = sampleRate;
    numChannels_ = std::clamp<std::size_t>(numChannels, 1, 2);
    frameSamples_ = std::max<std::size_t>(1, dsp::msToSamples(kFrameMs, sampleRate));
    reset();
}

void LevelAnalyzer::reset() noexcept {
    inFrame_ = 0;
    frameEnergy_ = 0.0;
    chEnergy_.fill(0.0);
    histogram_.fill(0);
    totalFrames_ = 0;
    totalSamples_ = 0;
    peak_ = 0.0;
}

void LevelAnalyzer::finishFrame() noexcept {
    // The louder channel represents the frame (matches the linked expander detector).
    double e = chEnergy_[0];
    if (numChannels_ > 1)
        e = std::max(e, chEnergy_[1]);
    const double ms = e / static_cast<double>(frameSamples_);
    const double db = std::clamp(dsp::powerToDb(ms), static_cast<double>(kMinDb), static_cast<double>(kMaxDb));
    const auto bin = static_cast<std::size_t>(std::lround((db - kMinDb) / kBinDb));
    ++histogram_[std::min(bin, kBins - 1)];
    ++totalFrames_;
    chEnergy_.fill(0.0);
    inFrame_ = 0;
}

void LevelAnalyzer::process(ConstAudioView audio) noexcept {
    const std::size_t nch = std::min(audio.numChannels, numChannels_);
    for (std::size_t i = 0; i < audio.numFrames; ++i) {
        for (std::size_t c = 0; c < nch; ++c) {
            const double x = static_cast<double>(audio.channels[c][i]);
            chEnergy_[c] += x * x;
            peak_ = std::max(peak_, std::fabs(x));
        }
        ++totalSamples_;
        if (++inFrame_ >= frameSamples_)
            finishFrame();
    }
}

LevelAnalysis LevelAnalyzer::result() const noexcept {
    LevelAnalysis r;
    r.durationSeconds = static_cast<double>(totalSamples_) / sampleRate_;
    r.peakDbfs = peak_ > 0.0 ? dsp::gainToDb(peak_) : -144.0;
    if (totalFrames_ == 0 || peak_ <= 0.0)
        return r;

    auto binDb = [](std::size_t b) { return kMinDb + static_cast<double>(b) * kBinDb; };
    auto binPower = [&](std::size_t b) { return dsp::dbToPower(binDb(b)); };

    // Absolute gate: ignore frames below -70 dBFS (digital silence, fades).
    const auto absGateBin = static_cast<std::size_t>((-70.0 - kMinDb) / kBinDb);
    double sumP = 0.0;
    std::uint64_t n = 0;
    for (std::size_t b = absGateBin; b < kBins; ++b) {
        sumP += static_cast<double>(histogram_[b]) * binPower(b);
        n += histogram_[b];
    }
    if (n == 0)
        return r;
    r.hasSignal = true;
    const double ungatedDb = dsp::powerToDb(sumP / static_cast<double>(n));

    // Two relative gates: 20 dB below the ungated mean removes pauses/silence, then 10 dB below
    // that mean (the BS.1770-4 relative gate) removes room tone and breaths. The energy mean of
    // what remains is the speech level; it is largely independent of how much silence the file has.
    auto gatedMean = [&](double gateDb, std::uint64_t& count) {
        const auto gateBin = static_cast<std::size_t>(std::lround((std::max(gateDb, -70.0) - kMinDb) / kBinDb));
        double p = 0.0;
        count = 0;
        for (std::size_t b = std::min(gateBin, kBins - 1); b < kBins; ++b) {
            p += static_cast<double>(histogram_[b]) * binPower(b);
            count += histogram_[b];
        }
        return count > 0 ? dsp::powerToDb(p / static_cast<double>(count)) : ungatedDb;
    };
    std::uint64_t n1 = 0, n2 = 0;
    const double stage1Db = gatedMean(ungatedDb - 20.0, n1);
    r.speechLevelDbfs = gatedMean(stage1Db - 10.0, n2);
    r.activeSeconds = static_cast<double>(n2) * static_cast<double>(frameSamples_) / sampleRate_;

    // Noise floor: energy mean of the quietest 10 % of frames that are not digital silence
    // (above -100 dBFS). Reported for operators and logs; the threshold mode does not use it.
    const auto floorStartBin = static_cast<std::size_t>((-100.0 - kMinDb) / kBinDb);
    std::uint64_t nonSilent = 0;
    for (std::size_t b = floorStartBin; b < kBins; ++b)
        nonSilent += histogram_[b];
    if (nonSilent > 0) {
        const std::uint64_t want = std::max<std::uint64_t>(1, nonSilent / 10);
        std::uint64_t taken = 0;
        double p = 0.0;
        for (std::size_t b = floorStartBin; b < kBins && taken < want; ++b) {
            const std::uint64_t k = std::min<std::uint64_t>(histogram_[b], want - taken);
            p += static_cast<double>(k) * binPower(b);
            taken += k;
        }
        r.noiseFloorDbfs = dsp::powerToDb(p / static_cast<double>(taken));
    }
    return r;
}

} // namespace radiff::core

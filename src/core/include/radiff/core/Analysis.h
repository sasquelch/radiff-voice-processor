#pragma once

#include "radiff/core/AudioBuffer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace radiff::core {

/// Result of a LevelAnalyzer pass over a recording (all levels dBFS, detector domain).
struct LevelAnalysis {
    double durationSeconds = 0.0;
    double peakDbfs = -144.0;           ///< sample peak
    double speechLevelDbfs = -144.0;    ///< gated RMS level of the active (speech) material
    double noiseFloorDbfs = -144.0;     ///< RMS level of the quietest 10 % of non-digital-silence frames
    double activeSeconds = 0.0;         ///< time spent above the relative gate
    bool hasSignal = false;             ///< false for digital silence / empty input
};

/// Offline-style level statistics for a whole recording, computed from 10 ms RMS frames.
///
/// Speech level follows the ITU-R BS.1770 gating idea (without K-weighting, because the value
/// feeds the expander's unweighted detector): frames below -70 dBFS are dropped, the mean of
/// the rest defines a relative gate 20 dB lower, and the energy mean of frames above that gate
/// is the speech level. Long pauses, room tone and breaths therefore do not pull it down.
///
/// Feed any number of blocks with process(); result() may be called at any time.
/// Allocation-free after prepare(); fine to run on an audio thread but intended for offline use.
class LevelAnalyzer {
public:
    static constexpr double kFrameMs = 10.0;
    static constexpr int kMinDb = -120, kMaxDb = 0;
    static constexpr double kBinDb = 0.25;
    static constexpr std::size_t kBins = static_cast<std::size_t>((kMaxDb - kMinDb) / kBinDb) + 1;

    void prepare(double sampleRate, std::size_t numChannels) noexcept;
    void reset() noexcept;
    void process(ConstAudioView audio) noexcept;
    [[nodiscard]] LevelAnalysis result() const noexcept;

private:
    void finishFrame() noexcept;

    double sampleRate_ = 48000.0;
    std::size_t numChannels_ = 1;
    std::size_t frameSamples_ = 480, inFrame_ = 0;
    double frameEnergy_ = 0.0; ///< sum of squares over channels (max channel used)
    std::array<double, 2> chEnergy_{};
    std::array<std::uint64_t, kBins> histogram_{};
    std::uint64_t totalFrames_ = 0, totalSamples_ = 0;
    double peak_ = 0.0;
};

} // namespace radiff::core

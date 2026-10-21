#pragma once

#include "radiff/core/AudioBuffer.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace radiff::ui {

/// Static facts about the loaded file (UI thread).
struct PlayerFileInfo {
    std::string name;            ///< file name (UTF-8), empty when nothing is loaded
    double sampleRate = 0.0;
    std::size_t numChannels = 0;
    std::uint64_t numFrames = 0;
    [[nodiscard]] double durationSeconds() const noexcept {
        return sampleRate > 0.0 ? static_cast<double>(numFrames) / sampleRate : 0.0;
    }
};

/// Toolkit-free preview player: a WAV file is loaded on the UI thread and rendered on the
/// audio thread in place of the live input, so the engine, meters and bypass behave exactly
/// as for a microphone.
///
/// Thread contract:
///  * load()/play()/pause()/stop()/seek()/setLooping() and all getters: UI thread.
///  * render(): audio thread only; never allocates, never locks.
///  * Publishing: two file slots. load() fills the slot the audio thread is NOT using and then
///    flips `active_`. render() announces the slot it reads in `reading_` and re-checks `active_`
///    after announcing, so (with sequentially consistent atomics) the UI thread can wait for
///    `reading_ != target` before overwriting a slot and never races a running render.
///  * Resampling: when the file and device rates are equal samples are copied bit-exactly;
///    otherwise a 32-tap windowed-sinc interpolator (Blackman window, cut-off at 95 % of the
///    lower Nyquist, kernel normalised per output sample) runs with a per-sample fractional
///    phase. The window table is built once in the constructor.
class FilePlayer {
public:
    static constexpr int kHalfTaps = 16; ///< taps on each side of the read position

    FilePlayer();

    /// Loads a WAV (mono/stereo, any supported encoding/rate). Returns "" on success, else the error.
    std::string load(const std::filesystem::path& file);
    [[nodiscard]] bool hasFile() const noexcept { return active_.load() >= 0; }
    [[nodiscard]] PlayerFileInfo info() const;

    void play() noexcept;
    void pause() noexcept { playing_.store(false); }
    void stop() noexcept;
    void togglePlay() noexcept;
    [[nodiscard]] bool isPlaying() const noexcept { return playing_.load(); }
    void setLooping(bool loop) noexcept { looping_.store(loop); }
    [[nodiscard]] bool looping() const noexcept { return looping_.load(); }
    /// Jumps to `fraction` (0..1) of the file; applied by the next render() (immediately when idle).
    void seek(double fraction) noexcept;
    [[nodiscard]] double positionSeconds() const noexcept;
    [[nodiscard]] double durationSeconds() const noexcept { return info().durationSeconds(); }
    /// True once after playback reached the end of a non-looping file.
    bool takeEnded() noexcept { return ended_.exchange(false); }

    /// Audio thread. Writes `numFrames` frames into `out[0..numChannels)` (mono files feed every
    /// channel). Returns false (and writes silence) when not playing or nothing is loaded.
    bool render(float* const* out, std::size_t numChannels, std::size_t numFrames, double deviceSampleRate) noexcept;

private:
    struct Slot {
        core::AudioBuffer audio;
        double sampleRate = 0.0;
        std::string name;
        std::uint64_t numFrames = 0;
    };
    static constexpr int kWindowPhases = 64; ///< window table resolution per tap

    [[nodiscard]] inline float interpolate(const float* x, std::uint64_t n, double pos, double fc) const noexcept;

    std::array<Slot, 2> slots_{};
    std::atomic<int> active_{-1};  ///< slot index the audio thread should read, -1 = none
    std::atomic<int> reading_{-1}; ///< slot index a render() is currently reading
    std::atomic<bool> playing_{false}, looping_{false}, ended_{false};
    std::atomic<double> position_{0.0};     ///< read position in file frames
    std::atomic<double> seekRequest_{-1.0}; ///< fraction 0..1, < 0 = none
    std::vector<float> window_;             ///< Blackman window, |t| in [0, kHalfTaps], kWindowPhases per tap
};

} // namespace radiff::ui

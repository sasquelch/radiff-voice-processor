#include "radiff/ui/FilePlayer.h"
#include "radiff/audioio/WavFile.h"
#include "radiff/platform/Paths.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace radiff::ui {

FilePlayer::FilePlayer() {
    // Blackman window over |t| <= kHalfTaps, sampled kWindowPhases times per tap (+1 guard).
    const std::size_t n = static_cast<std::size_t>(kHalfTaps * kWindowPhases) + 2;
    window_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kWindowPhases; // 0 .. kHalfTaps
        const double a = std::numbers::pi * std::min(t, static_cast<double>(kHalfTaps)) / kHalfTaps;
        window_[i] = static_cast<float>(0.42 + 0.5 * std::cos(a) + 0.08 * std::cos(2.0 * a));
    }
    window_.back() = 0.0f;
}

std::string FilePlayer::load(const std::filesystem::path& file) {
    audioio::WavInfo info;
    core::AudioBuffer audio;
    const auto err = audioio::readWav(file, info, audio);
    if (!err.ok())
        return err.message;
    if (audio.numChannels() == 0 || audio.numFrames() == 0)
        return "The file contains no audio";
    const int target = active_.load() == 0 ? 1 : 0;
    // Wait until no render() is reading the target slot (at most one audio block).
    while (reading_.load() == target) {
    }
    Slot& s = slots_[static_cast<std::size_t>(target)];
    s.audio = std::move(audio);
    s.sampleRate = info.sampleRate;
    s.numFrames = s.audio.numFrames();
    s.name = platform::pathToUtf8(file.filename());
    playing_.store(false);
    position_.store(0.0);
    seekRequest_.store(-1.0);
    ended_.store(false);
    active_.store(target);
    return {};
}

PlayerFileInfo FilePlayer::info() const {
    PlayerFileInfo i;
    const int a = active_.load();
    if (a < 0)
        return i;
    const Slot& s = slots_[static_cast<std::size_t>(a)];
    i.name = s.name;
    i.sampleRate = s.sampleRate;
    i.numChannels = s.audio.numChannels();
    i.numFrames = s.numFrames;
    return i;
}

void FilePlayer::play() noexcept {
    const int a = active_.load();
    if (a < 0)
        return;
    if (position_.load() >= static_cast<double>(slots_[static_cast<std::size_t>(a)].numFrames))
        seekRequest_.store(0.0); // play after the end restarts
    ended_.store(false);
    playing_.store(true);
}

void FilePlayer::stop() noexcept {
    playing_.store(false);
    seekRequest_.store(0.0);
    position_.store(0.0);
}

void FilePlayer::togglePlay() noexcept {
    if (playing_.load())
        pause();
    else
        play();
}

void FilePlayer::seek(double fraction) noexcept {
    const double f = std::clamp(fraction, 0.0, 1.0);
    seekRequest_.store(f);
    if (!playing_.load()) { // no render() will consume it soon: apply now for the display
        const int a = active_.load();
        if (a >= 0)
            position_.store(f * static_cast<double>(slots_[static_cast<std::size_t>(a)].numFrames));
    }
}

double FilePlayer::positionSeconds() const noexcept {
    const int a = active_.load();
    if (a < 0)
        return 0.0;
    const Slot& s = slots_[static_cast<std::size_t>(a)];
    return s.sampleRate > 0.0 ? position_.load() / s.sampleRate : 0.0;
}

inline float FilePlayer::interpolate(const float* x, std::uint64_t n, double pos, double fc) const noexcept {
    // y = sum_k x[i0 + k] * fc * sinc(fc * (k - frac)) * w(k - frac), k = -kHalfTaps+1 .. kHalfTaps,
    // normalised by the kernel sum so low frequencies pass at exactly unity gain.
    const double i0d = std::floor(pos);
    const double frac = pos - i0d;
    const auto i0 = static_cast<std::int64_t>(i0d);
    // sin(pi*fc*(k - frac)) by rotation: theta_k = theta_start + k*delta (one sin/cos per sample).
    const double delta = std::numbers::pi * fc;
    const int kStart = -kHalfTaps + 1;
    const double theta0 = delta * (static_cast<double>(kStart) - frac);
    double s = std::sin(theta0), c = std::cos(theta0);
    const double sd = std::sin(delta), cd = std::cos(delta);
    double acc = 0.0, norm = 0.0;
    for (int k = kStart; k <= kHalfTaps; ++k) {
        const double t = static_cast<double>(k) - frac; // distance from the read position, |t| < kHalfTaps
        const double wi = std::fabs(t) * kWindowPhases;
        const auto wi0 = static_cast<std::size_t>(wi);
        const float wf = static_cast<float>(wi - static_cast<double>(wi0));
        const float w = window_[wi0] + wf * (window_[wi0 + 1] - window_[wi0]);
        const double arg = delta * t; // pi*fc*t
        const double sinc = std::fabs(arg) < 1e-9 ? 1.0 : s / arg;
        const double h = fc * sinc * static_cast<double>(w);
        norm += h;
        const std::int64_t idx = i0 + k;
        if (idx >= 0 && static_cast<std::uint64_t>(idx) < n)
            acc += h * static_cast<double>(x[idx]);
        const double sn = s * cd + c * sd; // rotate theta by delta
        c = c * cd - s * sd;
        s = sn;
    }
    return static_cast<float>(norm > 1e-9 ? acc / norm : acc);
}

bool FilePlayer::render(float* const* out, std::size_t numChannels, std::size_t numFrames, double deviceSampleRate) noexcept {
    auto silence = [&] {
        for (std::size_t c = 0; c < numChannels; ++c)
            std::fill(out[c], out[c] + numFrames, 0.0f);
    };
    int a = active_.load();
    if (a < 0 || numChannels == 0) {
        silence();
        return false;
    }
    reading_.store(a);
    if (const int again = active_.load(); again != a) { // flipped between the two loads
        a = again;
        reading_.store(a);
    }
    const Slot& slot = slots_[static_cast<std::size_t>(a)];
    const double seekF = seekRequest_.exchange(-1.0);
    double pos = position_.load();
    if (seekF >= 0.0)
        pos = seekF * static_cast<double>(slot.numFrames);
    if (!playing_.load()) {
        position_.store(pos);
        reading_.store(-1);
        silence();
        return false;
    }
    const double total = static_cast<double>(slot.numFrames);
    const double step = deviceSampleRate > 0.0 ? slot.sampleRate / deviceSampleRate : 1.0;
    const bool exact = std::fabs(step - 1.0) < 1e-12;
    const double fc = std::min(1.0, 1.0 / step) * 0.95;
    const std::size_t fileCh = slot.audio.numChannels();
    const bool loop = looping_.load();
    bool stillPlaying = true;
    for (std::size_t i = 0; i < numFrames; ++i) {
        if (pos >= total) {
            if (loop && total > 0.0) {
                pos = std::fmod(pos, total);
            } else {
                stillPlaying = false;
                for (std::size_t c = 0; c < numChannels; ++c)
                    out[c][i] = 0.0f;
                continue;
            }
        }
        for (std::size_t c = 0; c < numChannels; ++c) {
            const float* x = slot.audio.channel(std::min(c, fileCh - 1)).data();
            out[c][i] = exact ? x[static_cast<std::size_t>(pos)] : interpolate(x, slot.numFrames, pos, fc);
        }
        pos += step;
    }
    if (!stillPlaying) {
        playing_.store(false);
        ended_.store(true);
        pos = total;
    }
    position_.store(pos);
    reading_.store(-1);
    return true;
}

} // namespace radiff::ui

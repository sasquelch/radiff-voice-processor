#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace radiff::core {

/// Non-owning view of de-interleaved float audio: one span per channel.
/// This is the only audio representation the engine understands; every host
/// (CLI, standalone, VST3, AU) adapts its own buffers to this.
struct AudioView {
    float* const* channels = nullptr;
    std::size_t numChannels = 0;
    std::size_t numFrames = 0;

    [[nodiscard]] std::span<float> channel(std::size_t index) const noexcept {
        return {channels[index], numFrames};
    }
};

struct ConstAudioView {
    const float* const* channels = nullptr;
    std::size_t numChannels = 0;
    std::size_t numFrames = 0;

    [[nodiscard]] std::span<const float> channel(std::size_t index) const noexcept {
        return {channels[index], numFrames};
    }
};

/// Owning de-interleaved buffer used by tests, the CLI and the app layer.
/// Not used inside process(): the engine never allocates on the audio thread.
class AudioBuffer {
public:
    AudioBuffer() = default;
    AudioBuffer(std::size_t numChannels, std::size_t numFrames) { resize(numChannels, numFrames); }

    void resize(std::size_t numChannels, std::size_t numFrames) {
        data_.assign(numChannels, std::vector<float>(numFrames, 0.0f));
        rebuildPointers();
    }

    void clear() {
        for (auto& ch : data_)
            std::fill(ch.begin(), ch.end(), 0.0f);
    }

    [[nodiscard]] std::size_t numChannels() const noexcept { return data_.size(); }
    [[nodiscard]] std::size_t numFrames() const noexcept { return data_.empty() ? 0 : data_[0].size(); }

    [[nodiscard]] std::span<float> channel(std::size_t i) noexcept { return data_[i]; }
    [[nodiscard]] std::span<const float> channel(std::size_t i) const noexcept { return data_[i]; }

    [[nodiscard]] AudioView view() noexcept { return {ptrs_.data(), numChannels(), numFrames()}; }
    [[nodiscard]] ConstAudioView constView() const noexcept {
        return {constPtrs_.data(), numChannels(), numFrames()};
    }

    /// View of a sub-range [start, start + length) of frames, for block-wise processing.
    [[nodiscard]] AudioView view(std::size_t start, std::size_t length) {
        subPtrs_.resize(numChannels());
        for (std::size_t c = 0; c < numChannels(); ++c)
            subPtrs_[c] = data_[c].data() + start;
        return {subPtrs_.data(), numChannels(), length};
    }

private:
    void rebuildPointers() {
        ptrs_.resize(data_.size());
        constPtrs_.resize(data_.size());
        for (std::size_t c = 0; c < data_.size(); ++c) {
            ptrs_[c] = data_[c].data();
            constPtrs_[c] = data_[c].data();
        }
    }

    std::vector<std::vector<float>> data_;
    std::vector<float*> ptrs_;
    std::vector<const float*> constPtrs_;
    std::vector<float*> subPtrs_;
};

} // namespace radiff::core

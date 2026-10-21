#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace radiff::core::dsp {

/// Fixed-maximum integer delay line (one channel). Memory is allocated in prepare() only.
class DelayLine {
public:
    void prepare(std::size_t maxDelaySamples) {
        buffer_.assign(maxDelaySamples + 1, 0.0f);
        reset();
    }
    void reset() noexcept {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        write_ = 0;
    }
    /// Sets the delay (clamped to the prepared maximum). Takes effect immediately.
    void setDelay(std::size_t samples) noexcept { delay_ = std::min(samples, buffer_.empty() ? 0 : buffer_.size() - 1); }
    [[nodiscard]] std::size_t delay() const noexcept { return delay_; }

    /// Writes x, returns the sample delayed by delay() samples.
    [[nodiscard]] inline float processSample(float x) noexcept {
        if (buffer_.empty())
            return x;
        buffer_[write_] = x;
        std::size_t read = write_ + buffer_.size() - delay_;
        if (read >= buffer_.size())
            read -= buffer_.size();
        const float y = buffer_[read];
        if (++write_ == buffer_.size())
            write_ = 0;
        return y;
    }

private:
    std::vector<float> buffer_;
    std::size_t write_ = 0, delay_ = 0;
};

} // namespace radiff::core::dsp

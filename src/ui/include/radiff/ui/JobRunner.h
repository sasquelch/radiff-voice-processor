#pragma once

#include "radiff/app/FileProcessor.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>

namespace radiff::ui {

/// Runs one app::processFile() job on a worker thread so a GUI never blocks.
/// The GUI polls progress() and takeResult() from its idle callback.
class JobRunner {
public:
    JobRunner() = default;
    ~JobRunner();
    JobRunner(const JobRunner&) = delete;
    JobRunner& operator=(const JobRunner&) = delete;

    /// Starts the job; returns false (and does nothing) while another job is running.
    bool start(app::FileJob job);
    [[nodiscard]] bool busy() const noexcept { return busy_.load(std::memory_order_acquire); }
    [[nodiscard]] double progress() const noexcept { return progress_.load(std::memory_order_relaxed); }
    /// The finished job's report, once; nullopt while running or already taken.
    std::optional<app::FileJobReport> takeResult();

private:
    void join();

    std::thread thread_;
    std::atomic<bool> busy_{false};
    std::atomic<double> progress_{0.0};
    std::mutex mutex_;
    std::optional<app::FileJobReport> result_;
};

/// "<input stem>-processed.wav" next to the input.
[[nodiscard]] std::filesystem::path defaultOutputPath(const std::filesystem::path& input);

} // namespace radiff::ui

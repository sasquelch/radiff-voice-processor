#include "radiff/ui/JobRunner.h"
#include "radiff/platform/Paths.h"

namespace radiff::ui {

JobRunner::~JobRunner() { join(); }

void JobRunner::join() {
    if (thread_.joinable())
        thread_.join();
}

bool JobRunner::start(app::FileJob job) {
    if (busy_.load(std::memory_order_acquire))
        return false;
    join(); // reap the previous worker
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result_.reset();
    }
    progress_.store(0.0, std::memory_order_relaxed);
    busy_.store(true, std::memory_order_release);
    thread_ = std::thread([this, job = std::move(job)]() {
        app::FileJobReport report =
            app::processFile(job, [this](double f) { progress_.store(f, std::memory_order_relaxed); });
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result_ = std::move(report);
        }
        progress_.store(1.0, std::memory_order_relaxed);
        busy_.store(false, std::memory_order_release);
    });
    return true;
}

std::optional<app::FileJobReport> JobRunner::takeResult() {
    if (busy_.load(std::memory_order_acquire))
        return std::nullopt;
    std::lock_guard<std::mutex> lock(mutex_);
    auto r = std::move(result_);
    result_.reset();
    return r;
}

std::filesystem::path defaultOutputPath(const std::filesystem::path& input) {
    std::string stem = platform::pathToUtf8(input.stem());
    auto out = input;
    out.replace_filename(platform::pathFromUtf8(stem + "-processed.wav"));
    return out;
}

} // namespace radiff::ui

#pragma once

#include "radiff/app/ExitCodes.h"
#include "radiff/audioio/WavFile.h"
#include "radiff/core/Parameters.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace radiff::app {

/// One offline processing job: input WAV -> engine -> output WAV, published transactionally.
/// Operational paths live here; sonic settings live in the ParameterSet (from a preset).
struct FileJob {
    std::filesystem::path input;
    std::filesystem::path output;
    core::ParameterSet parameters;

    /// Output encoding. Default: keep the input's encoding.
    std::optional<audioio::SampleFormat> outputFormat;
    /// Processing block size. Hosts process in blocks, so offline processing does too;
    /// output must not depend on this value (tested).
    std::size_t blockFrames = 512;
    /// Allow overwriting an existing output file.
    bool overwrite = true;
};

struct FileJobReport {
    ExitCode exitCode = ExitCode::Success;
    std::string message;        ///< error description or short success summary
    audioio::WavInfo inputInfo;
    std::uint64_t framesProcessed = 0;
    float peakOut = 0.0f;       ///< linear peak of the output, for logging/validation
};

using ProgressFn = std::function<void(double fraction)>;

/// Runs the job synchronously. Never throws; every failure becomes an ExitCode.
FileJobReport processFile(const FileJob& job, const ProgressFn& progress = {});

} // namespace radiff::app

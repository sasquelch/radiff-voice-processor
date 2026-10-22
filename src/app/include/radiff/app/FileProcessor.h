#pragma once

#include "radiff/app/ExitCodes.h"
#include "radiff/audioio/WavFile.h"
#include "radiff/core/Analysis.h"
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
    /// Allow replacing an existing output file (explicit opt-in; the source is never overwritten).
    bool overwrite = false;
    /// Measure only: run the analysis pass, fill FileJobReport::analysis and the resolved
    /// expander threshold, write nothing. `output` may be empty.
    bool analyzeOnly = false;
    /// > 0: before reading, wait up to this many seconds for the input to exist AND to stop
    /// growing (size unchanged over two polls 400 ms apart). For automation systems that call
    /// the processor while they are still writing the file. 0 = read immediately.
    double waitForInputSeconds = 0.0;
    /// Treat an input whose samples are all exactly zero as a failure (ExitCode::ValidationError)
    /// instead of producing a silent output. Off by default: silent inputs are legitimate in general,
    /// but a voice-track pipeline wants to know.
    bool rejectSilentInput = false;
};

struct FileJobReport {
    ExitCode exitCode = ExitCode::Success;
    std::string message;        ///< error description or short success summary
    audioio::WavInfo inputInfo;
    std::uint64_t framesProcessed = 0;
    std::size_t latencyFrames = 0; ///< engine latency that was compensated
    float peakIn = 0.0f;        ///< linear peak of the input
    float peakOut = 0.0f;       ///< linear peak of the output, for logging/validation
    /// Level statistics of the input as seen by the expander detector (after input stage + EQ).
    core::LevelAnalysis analysis;
    /// Expander threshold actually used (dBFS) after resolving the threshold mode.
    double expanderThresholdDbfs = 0.0;
    bool expanderThresholdIsRelative = false;
    /// True when every input sample was exactly zero (digital silence).
    bool inputSilent = false;
    /// Seconds spent in the waitForInputSeconds loop (0 when not waiting).
    double waitedSeconds = 0.0;
};

using ProgressFn = std::function<void(double fraction)>;

/// Runs the job synchronously. Never throws; every failure becomes an ExitCode.
FileJobReport processFile(const FileJob& job, const ProgressFn& progress = {});

} // namespace radiff::app

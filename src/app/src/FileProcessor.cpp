#include "radiff/app/FileProcessor.h"
#include "radiff/app/TransactionalFile.h"
#include "radiff/core/Engine.h"
#include "radiff/platform/Paths.h"

#include <algorithm>
#include <cmath>
#include <exception>

namespace radiff::app {

const char* describe(ExitCode c) noexcept {
    switch (c) {
    case ExitCode::Success: return "success";
    case ExitCode::UsageError: return "usage error";
    case ExitCode::InputError: return "input error";
    case ExitCode::PresetError: return "preset error";
    case ExitCode::ProcessingError: return "processing error";
    case ExitCode::OutputError: return "output error";
    case ExitCode::InternalError: return "internal error";
    }
    return "unknown";
}

namespace {

FileJobReport fail(ExitCode code, std::string message) {
    FileJobReport r;
    r.exitCode = code;
    r.message = std::move(message);
    return r;
}

} // namespace

FileJobReport processFile(const FileJob& job, const ProgressFn& progress) {
    try {
        std::error_code ec;
        if (job.input.empty())
            return fail(ExitCode::UsageError, "No input file specified");
        if (job.output.empty())
            return fail(ExitCode::UsageError, "No output file specified");
        if (!std::filesystem::exists(job.input, ec))
            return fail(ExitCode::InputError, "Input file does not exist: " + platform::pathToUtf8(job.input));
        if (std::filesystem::equivalent(job.input, job.output, ec))
            return fail(ExitCode::UsageError, "Input and output must be different files");
        if (!job.overwrite && std::filesystem::exists(job.output, ec))
            return fail(ExitCode::OutputError, "Output file already exists: " + platform::pathToUtf8(job.output));
        if (job.blockFrames == 0)
            return fail(ExitCode::UsageError, "Block size must be greater than zero");

        FileJobReport report;
        core::AudioBuffer audio;
        if (auto e = audioio::readWav(job.input, report.inputInfo, audio); !e.ok())
            return fail(ExitCode::InputError, e.message);
        if (report.inputInfo.numChannels > 2)
            return fail(ExitCode::InputError, "Only mono and stereo input is supported (file has " +
                                                  std::to_string(report.inputInfo.numChannels) + " channels)");

        core::Engine engine;
        core::EngineConfig cfg;
        cfg.sampleRate = report.inputInfo.sampleRate;
        cfg.maxBlockFrames = job.blockFrames;
        cfg.numChannels = audio.numChannels();
        engine.prepare(cfg);
        engine.setParameters(job.parameters);

        const std::size_t total = audio.numFrames();
        for (std::size_t pos = 0; pos < total; pos += job.blockFrames) {
            const std::size_t n = std::min(job.blockFrames, total - pos);
            engine.process(audio.view(pos, n));
            if (progress)
                progress(static_cast<double>(pos + n) / static_cast<double>(std::max<std::size_t>(total, 1)));
        }
        engine.release();
        report.framesProcessed = total;

        // Numerical safety: a broadcast file with NaN/inf must never be published.
        for (std::size_t c = 0; c < audio.numChannels(); ++c) {
            for (float v : audio.channel(c)) {
                if (!std::isfinite(v))
                    return fail(ExitCode::ProcessingError, "DSP produced a non-finite sample; output withheld");
                report.peakOut = std::max(report.peakOut, std::fabs(v));
            }
        }

        const audioio::SampleFormat outFormat = job.outputFormat.value_or(report.inputInfo.format);
        const double sampleRate = report.inputInfo.sampleRate;
        const auto expectedFrames = static_cast<std::uint64_t>(total);
        const auto expectedChannels = static_cast<std::uint16_t>(audio.numChannels());

        auto write = [&](const std::filesystem::path& temp) -> std::string {
            auto e = audioio::writeWav(temp, sampleRate, outFormat, audio);
            return e.message;
        };
        auto validate = [&](const std::filesystem::path& temp) -> std::string {
            audioio::WavInfo check;
            if (auto e = audioio::readWavInfo(temp, check); !e.ok())
                return e.message;
            if (check.numFrames != expectedFrames)
                return "frame count mismatch (" + std::to_string(check.numFrames) + " != " +
                       std::to_string(expectedFrames) + ")";
            if (check.numChannels != expectedChannels)
                return "channel count mismatch";
            if (check.sampleRate != sampleRate)
                return "sample rate mismatch";
            return {};
        };
        if (auto r = writeFileTransactionally(job.output, write, validate); !r.ok())
            return fail(ExitCode::OutputError, r.error);

        report.message = "Processed " + std::to_string(total) + " frames, " + std::to_string(audio.numChannels()) +
                         " ch @ " + std::to_string(static_cast<long long>(sampleRate)) + " Hz";
        return report;
    } catch (const std::exception& e) {
        return fail(ExitCode::InternalError, std::string("Unexpected error: ") + e.what());
    } catch (...) {
        return fail(ExitCode::InternalError, "Unexpected error");
    }
}

} // namespace radiff::app

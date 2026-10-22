#include "radiff/app/FileProcessor.h"
#include "radiff/audioio/AudioFile.h"
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
    case ExitCode::ValidationError: return "validation error";
    case ExitCode::Cancelled: return "cancelled";
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
        if (job.output.empty() && !job.analyzeOnly)
            return fail(ExitCode::UsageError, "No output file specified");
        if (!std::filesystem::exists(job.input, ec))
            return fail(ExitCode::InputError, "Input file does not exist: " + platform::pathToUtf8(job.input));
        if (!job.analyzeOnly) {
            if (std::filesystem::equivalent(job.input, job.output, ec))
                return fail(ExitCode::UsageError, "Input and output must be different files");
            if (audioio::looksLikeMp3(job.output) && !std::filesystem::exists(job.output, ec))
                return fail(ExitCode::UsageError,
                            "Output is always WAV; give the output a .wav name (MP3 is accepted as input only)");
            if (!job.overwrite && std::filesystem::exists(job.output, ec))
                return fail(ExitCode::OutputError, "Output file already exists: " + platform::pathToUtf8(job.output));
        }
        if (job.blockFrames == 0)
            return fail(ExitCode::UsageError, "Block size must be greater than zero");

        FileJobReport report;
        core::AudioBuffer audio;
        if (auto e = audioio::readAudio(job.input, report.inputInfo, audio); !e.ok())
            return fail(ExitCode::InputError, e.message);
        if (report.inputInfo.numChannels > 2)
            return fail(ExitCode::InputError, "Only mono and stereo input is supported (file has " +
                                                  std::to_string(report.inputInfo.numChannels) + " channels)");

        float inputPeak = 0.0f;
        for (std::size_t c = 0; c < audio.numChannels(); ++c)
            for (float v : audio.channel(c))
                inputPeak = std::max(inputPeak, std::fabs(v));

        // A fresh, fully independent engine per job: concurrent jobs never share state.
        core::Engine engine;
        core::EngineConfig cfg;
        cfg.sampleRate = report.inputInfo.sampleRate;
        cfg.maxBlockFrames = job.blockFrames;
        cfg.numChannels = audio.numChannels();
        const std::size_t total = audio.numFrames();

        // Analysis pass: level statistics of what the expander detector will see (input stage
        // and EQ applied, dynamics off). Drives the speechRelative threshold mode and the report.
        {
            core::ParameterSet measure = job.parameters;
            measure.set(core::ParamId::Bypass, 0.0f);
            measure.set(core::ParamId::ExpEnabled, 0.0f);
            measure.set(core::ParamId::ExpLookaheadMs, 0.0f);
            measure.set(core::ParamId::ExpTransientRejectMs, 0.0f);
            measure.set(core::ParamId::DeessEnabled, 0.0f);
            measure.set(core::ParamId::Comp1Enabled, 0.0f);
            measure.set(core::ParamId::Comp2Enabled, 0.0f);
            measure.set(core::ParamId::PresEnabled, 0.0f);
            measure.set(core::ParamId::OutputGainDb, 0.0f);
            core::AudioBuffer copy(audio);
            engine.prepare(cfg);
            engine.setParameters(measure);
            core::LevelAnalyzer analyzer;
            analyzer.prepare(cfg.sampleRate, cfg.numChannels);
            for (std::size_t pos = 0; pos < total; pos += job.blockFrames) {
                const std::size_t n = std::min(job.blockFrames, total - pos);
                auto v = copy.view(pos, n);
                engine.process(v);
                analyzer.process(core::ConstAudioView{v.channels, v.numChannels, v.numFrames});
            }
            report.analysis = analyzer.result();
            engine.release();
        }

        engine.prepare(cfg);
        engine.setParameters(job.parameters);
        if (report.analysis.hasSignal)
            engine.setSpeechLevelDb(report.analysis.speechLevelDbfs);
        report.expanderThresholdDbfs = engine.expander().effectiveThresholdDb();
        report.expanderThresholdIsRelative =
            job.parameters.getInt(core::ParamId::ExpThresholdMode) == static_cast<int>(core::ThresholdMode::SpeechRelative) &&
            report.analysis.hasSignal;
        const std::size_t latency = engine.latencyFrames();
        report.latencyFrames = latency;
        report.peakIn = inputPeak;

        if (job.analyzeOnly) {
            engine.release();
            report.message = "Analyzed " + std::to_string(total) + " frames, " + std::to_string(audio.numChannels()) +
                             " ch @ " + std::to_string(static_cast<long long>(cfg.sampleRate)) + " Hz";
            return report;
        }

        for (std::size_t pos = 0; pos < total; pos += job.blockFrames) {
            const std::size_t n = std::min(job.blockFrames, total - pos);
            engine.process(audio.view(pos, n));
            if (progress)
                progress(static_cast<double>(pos + n) / static_cast<double>(std::max<std::size_t>(total, 1)));
        }
        // Latency compensation: flush `latency` frames of silence through the engine and
        // shift the result so output frame i corresponds to input frame i.
        if (latency > 0) {
            core::AudioBuffer tail(audio.numChannels(), latency);
            for (std::size_t pos = 0; pos < latency; pos += job.blockFrames) {
                const std::size_t n = std::min(job.blockFrames, latency - pos);
                engine.process(tail.view(pos, n));
            }
            for (std::size_t c = 0; c < audio.numChannels(); ++c) {
                auto ch = audio.channel(c);
                const std::size_t keep = total > latency ? total - latency : 0;
                for (std::size_t i = 0; i < keep; ++i)
                    ch[i] = ch[i + latency];
                for (std::size_t i = keep; i < total; ++i)
                    ch[i] = tail.channel(c)[i - keep];
            }
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
        report.peakIn = inputPeak;
        // Unexpected silence: speech went in (above -60 dBFS) but nothing came out.
        // Legitimately silent inputs pass through this check untouched.
        if (inputPeak > 0.001f && report.peakOut < 1e-6f)
            return fail(ExitCode::ValidationError, "Output is silent although the input contains signal; output withheld");

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
            return fail(r.error.rfind("Output validation failed", 0) == 0 ? ExitCode::ValidationError : ExitCode::OutputError,
                        r.error);

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

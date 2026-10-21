// RaDiffVoiceCLI - headless, display-less, audio-device-less batch processor.
//
//   RaDiffVoiceCLI --input <in.wav> --output <out.wav> [--preset <file.radvoice.json>]
//                  [--set module.key=value ...] [--format pcm16|pcm24|pcm32|float32]
//                  [--bit-depth 16|24|32|float] [--overwrite] [--block <frames>]
//                  [--analyze-only] [--json-result] [--verbose] [--quiet] [--list-params]
//                  [--version] [--help]
//
// Exit codes: see radiff/app/ExitCodes.h (0 success, 2 usage, 3 input, 4 preset,
// 5 processing, 6 output I/O, 7 validation, 8 cancelled, 70 internal).

#include "radiff/app/ExitCodes.h"
#include "radiff/app/FileProcessor.h"
#include "radiff/core/Version.h"
#include "radiff/platform/Paths.h"
#include "radiff/platform/Process.h"
#include "radiff/platform/Strings.h"
#include "radiff/presets/Preset.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using radiff::app::ExitCode;

void printUsage(std::FILE* to) {
    std::fputs(
        "RaDiff Voice Processor CLI " RADIFF_VERSION_STRING "\n"
        "\n"
        "Usage:\n"
        "  RaDiffVoiceCLI --input <in.wav> --output <out.wav> --preset <file.radvoice.json> [options]\n"
        "\n"
        "Options:\n"
        "  --input <path>        Input WAV (16/24/32-bit PCM or 32-bit float, mono/stereo, any rate)\n"
        "  --output <path>       Output WAV; written to a temp file, verified, then atomically published\n"
        "  --preset <path>       Preset (.radvoice.json). Omitted = built-in balanced defaults\n"
        "  --set <path>=<value>  Override a parameter after the preset, e.g. --set expander.thresholdDbfs=-44\n"
        "                        (repeatable; see --list-params; enums by name, booleans true/false)\n"
        "  --format <fmt>        Output encoding: pcm16, pcm24, pcm32, float32 (default: same as input)\n"
        "  --bit-depth <n>       Same as --format: 16, 24, 32 (integer) or float\n"
        "  --overwrite           Allow replacing an existing output file (never the input)\n"
        "  --block <frames>      Processing block size, 1..65536 (default 512; does not affect output)\n"
        "  --analyze-only        Measure the input (speech level, noise floor, peak, resolved expander\n"
        "                        threshold) and write nothing; --output is optional\n"
        "  --json-result         Print a machine-readable JSON result object on stdout\n"
        "  --verbose             Print latency, meters and timing\n"
        "  --quiet               Only print errors\n"
        "  --list-params         Print all parameter paths with ranges and defaults\n"
        "  --version             Print version and exit\n"
        "  --help                Show this help\n"
        "\n"
        "Exit codes: 0 ok, 2 usage, 3 input, 4 preset, 5 processing, 6 output I/O, 7 validation,\n"
        "            8 cancelled, 70 internal\n",
        to);
}

void printParams() {
    for (const auto& p : radiff::core::paramTable()) {
        const std::string path = radiff::core::paramPath(p.id);
        if (p.isEnum()) {
            std::string names;
            for (auto n : p.enumNames)
                names += (names.empty() ? "" : "|") + std::string(n);
            std::printf("%-40s %-18s %s (default %s)\n", path.c_str(), std::string(p.name).c_str(), names.c_str(),
                        std::string(radiff::core::enumName(p.id, p.defaultValue)).c_str());
        } else if (p.isBoolean()) {
            std::printf("%-40s %-18s true|false (default %s)\n", path.c_str(), std::string(p.name).c_str(),
                        p.defaultValue >= 0.5f ? "true" : "false");
        } else {
            std::printf("%-40s %-18s %g..%g (default %g)\n", path.c_str(), std::string(p.name).c_str(),
                        static_cast<double>(p.minValue), static_cast<double>(p.maxValue),
                        static_cast<double>(p.defaultValue));
        }
    }
}

std::optional<radiff::audioio::SampleFormat> parseFormat(std::string_view s) {
    using F = radiff::audioio::SampleFormat;
    if (s == "pcm16" || s == "16") return F::Pcm16;
    if (s == "pcm24" || s == "24") return F::Pcm24;
    if (s == "pcm32" || s == "32") return F::Pcm32;
    if (s == "float32" || s == "float") return F::Float32;
    return std::nullopt;
}

struct Options {
    radiff::app::FileJob job;
    std::optional<std::string> presetPath;
    std::vector<std::pair<std::string, std::string>> overrides;
    bool quiet = false, verbose = false, jsonResult = false;
};

int emit(const Options& o, ExitCode code, const std::string& msg, const radiff::app::FileJobReport* report,
         double seconds) {
    if (o.jsonResult) {
        nlohmann::ordered_json j;
        j["ok"] = code == ExitCode::Success;
        j["exitCode"] = radiff::app::toInt(code);
        j["status"] = radiff::app::describe(code);
        j["message"] = msg;
        j["input"] = radiff::platform::pathToUtf8(o.job.input);
        j["output"] = radiff::platform::pathToUtf8(o.job.output);
        if (report) {
            j["analyzeOnly"] = o.job.analyzeOnly;
            j["frames"] = report->framesProcessed;
            j["sampleRate"] = report->inputInfo.sampleRate;
            j["channels"] = report->inputInfo.numChannels;
            j["latencyFramesCompensated"] = report->latencyFrames;
            j["inputPeakDbfs"] = report->peakIn > 0.0f ? 20.0 * std::log10(static_cast<double>(report->peakIn)) : -144.0;
            if (!o.job.analyzeOnly)
                j["outputPeakDbfs"] = report->peakOut > 0.0f ? 20.0 * std::log10(static_cast<double>(report->peakOut)) : -144.0;
            if (report->inputInfo.numFrames > 0) {
                nlohmann::ordered_json a;
                a["durationSeconds"] = report->analysis.durationSeconds;
                a["hasSignal"] = report->analysis.hasSignal;
                a["peakDbfs"] = report->analysis.peakDbfs;
                a["speechLevelDbfs"] = report->analysis.speechLevelDbfs;
                a["noiseFloorDbfs"] = report->analysis.noiseFloorDbfs;
                a["activeSeconds"] = report->analysis.activeSeconds;
                a["expanderThresholdDbfs"] = report->expanderThresholdDbfs;
                a["expanderThresholdMode"] = report->expanderThresholdIsRelative ? "speechRelative" : "absolute";
                j["analysis"] = a;
            }
        }
        j["seconds"] = seconds;
        std::printf("%s\n", j.dump().c_str());
    }
    if (code != ExitCode::Success) {
        std::fprintf(stderr, "error: %s\n", msg.c_str());
    } else if (!o.quiet && !o.jsonResult) {
        if (o.job.analyzeOnly)
            std::printf("%s: %s\n", radiff::platform::pathToUtf8(o.job.input).c_str(), msg.c_str());
        else
            std::printf("%s -> %s: %s (in %.1f dBFS, out %.1f dBFS)\n", radiff::platform::pathToUtf8(o.job.input).c_str(),
                        radiff::platform::pathToUtf8(o.job.output).c_str(), msg.c_str(),
                        report && report->peakIn > 0.0f ? 20.0 * std::log10(static_cast<double>(report->peakIn)) : -144.0,
                        report && report->peakOut > 0.0f ? 20.0 * std::log10(static_cast<double>(report->peakOut)) : -144.0);
        if ((o.verbose || o.job.analyzeOnly) && report) {
            const auto& a = report->analysis;
            std::printf("analysis: %.1f s, peak %.1f dBFS, speech level %.1f dBFS, noise floor %.1f dBFS, active %.1f s\n",
                        a.durationSeconds, a.peakDbfs, a.speechLevelDbfs, a.noiseFloorDbfs, a.activeSeconds);
            std::printf("expander threshold: %.1f dBFS (%s)\n", report->expanderThresholdDbfs,
                        report->expanderThresholdIsRelative ? "speech-relative" : "absolute");
        }
        if (o.verbose && report && !o.job.analyzeOnly)
            std::printf("latency compensated: %zu frames; elapsed %.3f s (%.0fx real time)\n", report->latencyFrames,
                        seconds,
                        seconds > 0.0 ? (static_cast<double>(report->framesProcessed) / report->inputInfo.sampleRate) / seconds : 0.0);
    }
    return radiff::app::toInt(code);
}

int usageError(const std::string& msg) {
    std::fprintf(stderr, "error: %s\n", msg.c_str());
    return radiff::app::toInt(ExitCode::UsageError);
}

int run(const std::vector<std::string>& args) {
    Options o;
    auto need = [&](std::size_t i, std::string_view flag) -> const std::string* {
        if (i + 1 >= args.size()) {
            std::fprintf(stderr, "error: %.*s requires a value\n", static_cast<int>(flag.size()), flag.data());
            return nullptr;
        }
        return &args[i + 1];
    };

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--help" || a == "-h") { printUsage(stdout); return 0; }
        if (a == "--version") { std::printf("%s\n", radiff::core::kVersionString); return 0; }
        if (a == "--list-params") { printParams(); return 0; }
        if (a == "--quiet") { o.quiet = true; continue; }
        if (a == "--verbose") { o.verbose = true; continue; }
        if (a == "--json-result") { o.jsonResult = true; continue; }
        if (a == "--overwrite") { o.job.overwrite = true; continue; }
        if (a == "--analyze-only") { o.job.analyzeOnly = true; continue; }
        if (a == "--input") { auto v = need(i, a); if (!v) return 2; o.job.input = radiff::platform::pathFromUtf8(*v); ++i; continue; }
        if (a == "--output") { auto v = need(i, a); if (!v) return 2; o.job.output = radiff::platform::pathFromUtf8(*v); ++i; continue; }
        if (a == "--preset") { auto v = need(i, a); if (!v) return 2; o.presetPath = *v; ++i; continue; }
        if (a == "--format" || a == "--bit-depth") {
            auto v = need(i, a); if (!v) return 2;
            auto f = parseFormat(*v);
            if (!f) return usageError("Unknown " + a + " '" + *v + "'");
            o.job.outputFormat = f; ++i; continue;
        }
        if (a == "--block") {
            auto v = need(i, a); if (!v) return 2;
            const auto n = radiff::platform::parseInteger(*v);
            if (!n || *n <= 0 || *n > 65536)
                return usageError("--block must be an integer between 1 and 65536");
            o.job.blockFrames = static_cast<std::size_t>(*n); ++i; continue;
        }
        if (a == "--set") {
            auto v = need(i, a); if (!v) return 2;
            const auto eq = v->find('=');
            if (eq == std::string::npos || eq == 0)
                return usageError("--set expects module.key=value, got '" + *v + "'");
            o.overrides.emplace_back(v->substr(0, eq), v->substr(eq + 1)); ++i; continue;
        }
        std::fprintf(stderr, "error: unknown argument '%s'\n\n", a.c_str());
        printUsage(stderr);
        return 2;
    }

    if (o.job.input.empty() || (o.job.output.empty() && !o.job.analyzeOnly)) {
        std::fputs("error: --input and --output are required (--output optional with --analyze-only)\n\n", stderr);
        printUsage(stderr);
        return 2;
    }

    // Sonic settings: preset, then overrides.
    radiff::presets::Preset preset;
    if (o.presetPath) {
        auto r = radiff::presets::loadPresetFile(radiff::platform::pathFromUtf8(*o.presetPath), preset);
        if (!r.ok())
            return emit(o, ExitCode::PresetError, r.error, nullptr, 0.0);
        if (!o.quiet)
            for (const auto& w : r.warnings)
                std::fprintf(stderr, "warning: %s\n", w.c_str());
    }
    for (const auto& [key, value] : o.overrides) {
        const auto id = radiff::core::paramIdForPath(key);
        if (!id)
            return usageError("Unknown parameter '" + key + "' (see --list-params)");
        const auto f = radiff::core::parseParamValue(*id, value);
        if (!f)
            return usageError("Invalid value '" + value + "' for parameter '" + key + "'");
        preset.parameters.set(*id, *f);
    }
    o.job.parameters = preset.parameters;

    const auto t0 = std::chrono::steady_clock::now();
    const auto report = radiff::app::processFile(o.job);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return emit(o, report.exitCode, report.message, &report, seconds);
}

} // namespace

int main(int argc, char** argv) {
    radiff::platform::prepareConsoleUtf8();
    try {
        return run(radiff::platform::commandLineUtf8(argc, argv));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: unexpected exception: %s\n", e.what());
        return radiff::app::toInt(ExitCode::InternalError);
    }
}

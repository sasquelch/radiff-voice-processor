// RaDiffVoiceCLI - headless, display-less, audio-device-less batch processor.
//
//   RaDiffVoiceCLI --input <in.wav> --output <out.wav> [--preset <file.radvoice.json>]
//                  [--set key=value ...] [--format pcm16|pcm24|pcm32|float32]
//                  [--block <frames>] [--no-overwrite] [--quiet] [--version] [--help]
//
// Exit codes: see radiff/app/ExitCodes.h (0 success, 2 usage, 3 input, 4 preset,
// 5 processing, 6 output, 70 internal).

#include "radiff/app/ExitCodes.h"
#include "radiff/app/FileProcessor.h"
#include "radiff/core/Version.h"
#include "radiff/platform/Paths.h"
#include "radiff/platform/Process.h"
#include "radiff/platform/Strings.h"
#include "radiff/presets/Preset.h"

#include <cmath>
#include <cstdio>
#include <iostream>
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
        "  RaDiffVoiceCLI --input <in.wav> --output <out.wav> [--preset <file.radvoice.json>] [options]\n"
        "\n"
        "Options:\n"
        "  --input <path>      Input WAV file (16/24/32-bit PCM or 32-bit float, mono or stereo)\n"
        "  --output <path>     Output WAV file; written atomically, never left partial\n"
        "  --preset <path>     Preset (.radvoice.json). Omit for factory defaults\n"
        "  --set key=value     Override a parameter after the preset is loaded (repeatable)\n"
        "  --format <fmt>      Output encoding: pcm16, pcm24, pcm32, float32 (default: same as input)\n"
        "  --block <frames>    Processing block size (default 512; does not affect output)\n"
        "  --no-overwrite      Fail instead of replacing an existing output file\n"
        "  --list-params       Print all parameter keys with ranges and defaults\n"
        "  --quiet             Only print errors\n"
        "  --version           Print version and exit\n"
        "  --help              Show this help\n"
        "\n"
        "Exit codes: 0 ok, 2 usage, 3 input, 4 preset, 5 processing, 6 output, 70 internal\n",
        to);
}

void printParams() {
    for (const auto& p : radiff::core::paramTable())
        std::printf("%-20s %-16s min=%-9g max=%-9g default=%g\n", std::string(p.key).c_str(),
                    std::string(p.name).c_str(), static_cast<double>(p.minValue), static_cast<double>(p.maxValue),
                    static_cast<double>(p.defaultValue));
}

std::optional<radiff::audioio::SampleFormat> parseFormat(std::string_view s) {
    using F = radiff::audioio::SampleFormat;
    if (s == "pcm16") return F::Pcm16;
    if (s == "pcm24") return F::Pcm24;
    if (s == "pcm32") return F::Pcm32;
    if (s == "float32") return F::Float32;
    return std::nullopt;
}

int fail(ExitCode code, const std::string& msg) {
    std::fprintf(stderr, "error: %s\n", msg.c_str());
    return radiff::app::toInt(code);
}

int run(const std::vector<std::string>& args) {
    radiff::app::FileJob job;
    std::optional<std::string> presetPath;
    std::vector<std::pair<std::string, std::string>> overrides;
    bool quiet = false;

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
        if (a == "--quiet") { quiet = true; continue; }
        if (a == "--no-overwrite") { job.overwrite = false; continue; }
        if (a == "--input") { auto v = need(i, a); if (!v) return 2; job.input = radiff::platform::pathFromUtf8(*v); ++i; continue; }
        if (a == "--output") { auto v = need(i, a); if (!v) return 2; job.output = radiff::platform::pathFromUtf8(*v); ++i; continue; }
        if (a == "--preset") { auto v = need(i, a); if (!v) return 2; presetPath = *v; ++i; continue; }
        if (a == "--format") {
            auto v = need(i, a); if (!v) return 2;
            auto f = parseFormat(*v);
            if (!f) return fail(ExitCode::UsageError, "Unknown --format '" + *v + "'");
            job.outputFormat = f; ++i; continue;
        }
        if (a == "--block") {
            auto v = need(i, a); if (!v) return 2;
            const auto n = radiff::platform::parseInteger(*v);
            if (!n || *n <= 0 || *n > 65536)
                return fail(ExitCode::UsageError, "--block must be an integer between 1 and 65536");
            job.blockFrames = static_cast<std::size_t>(*n); ++i; continue;
        }
        if (a == "--set") {
            auto v = need(i, a); if (!v) return 2;
            const auto eq = v->find('=');
            if (eq == std::string::npos || eq == 0)
                return fail(ExitCode::UsageError, "--set expects key=value, got '" + *v + "'");
            overrides.emplace_back(v->substr(0, eq), v->substr(eq + 1)); ++i; continue;
        }
        std::fprintf(stderr, "error: unknown argument '%s'\n\n", a.c_str());
        printUsage(stderr);
        return 2;
    }

    if (job.input.empty() || job.output.empty()) {
        std::fputs("error: --input and --output are required\n\n", stderr);
        printUsage(stderr);
        return 2;
    }

    // Sonic settings: preset, then overrides.
    radiff::presets::Preset preset;
    if (presetPath) {
        auto r = radiff::presets::loadPresetFile(radiff::platform::pathFromUtf8(*presetPath), preset);
        if (!r.ok())
            return fail(ExitCode::PresetError, r.error);
        if (!quiet)
            for (const auto& w : r.warnings)
                std::fprintf(stderr, "warning: %s\n", w.c_str());
    }
    for (const auto& [key, value] : overrides) {
        const auto id = radiff::core::paramIdForKey(key);
        if (!id)
            return fail(ExitCode::UsageError, "Unknown parameter '" + key + "' (see --list-params)");
        const auto f = radiff::platform::parseNumber(value);
        if (!f)
            return fail(ExitCode::UsageError, "Parameter '" + key + "' needs a numeric value, got '" + value + "'");
        preset.parameters.set(*id, static_cast<float>(*f));
    }
    job.parameters = preset.parameters;

    const auto report = radiff::app::processFile(job);
    if (report.exitCode != ExitCode::Success)
        return fail(report.exitCode, report.message);
    if (!quiet)
        std::printf("%s -> %s: %s (peak %.2f dBFS)\n", radiff::platform::pathToUtf8(job.input).c_str(),
                    radiff::platform::pathToUtf8(job.output).c_str(), report.message.c_str(),
                    report.peakOut > 0.0f ? 20.0 * std::log10(static_cast<double>(report.peakOut)) : -144.0);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    radiff::platform::prepareConsoleUtf8();
    try {
        return run(radiff::platform::commandLineUtf8(argc, argv));
    } catch (const std::exception& e) {
        return fail(ExitCode::InternalError, std::string("unexpected exception: ") + e.what());
    }
}

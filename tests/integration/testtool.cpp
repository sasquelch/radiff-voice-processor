// radiff_testtool - portable helper for CTest-driven integration tests.
//   gen-wav     --out <p> --rate <hz> --channels <n> --seconds <s> --format <f> --freq <hz> --amp <a>
//   gen-preset  --out <p> --name <n> [--set key=value ...]
//   verify-wav  --file <p> [--rate] [--channels] [--frames] [--format] [--peak-db <db> --tol-db <db>]
//   compare-wav --a <p> --b <p> --tol <max abs diff>
//   expect-exit <code> -- <command> [args...]

#include "radiff/audioio/WavFile.h"
#include "radiff/platform/Paths.h"
#include "radiff/platform/Process.h"
#include "radiff/presets/Preset.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <numbers>
#include <string>
#include <vector>

namespace {

using radiff::audioio::SampleFormat;

struct Args {
    std::map<std::string, std::string> kv;
    std::vector<std::string> sets;
    std::vector<std::string> rest;
};

Args parse(const std::vector<std::string>& a, std::size_t from) {
    Args out;
    for (std::size_t i = from; i < a.size(); ++i) {
        if (a[i] == "--") {
            out.rest.assign(a.begin() + static_cast<std::ptrdiff_t>(i + 1), a.end());
            break;
        }
        if (a[i].rfind("--", 0) == 0 && i + 1 < a.size()) {
            if (a[i] == "--set")
                out.sets.push_back(a[i + 1]);
            else
                out.kv[a[i].substr(2)] = a[i + 1];
            ++i;
        }
    }
    return out;
}

int die(const std::string& m) {
    std::fprintf(stderr, "testtool: %s\n", m.c_str());
    return 1;
}

SampleFormat formatFrom(const std::string& s) {
    if (s == "pcm16") return SampleFormat::Pcm16;
    if (s == "pcm24") return SampleFormat::Pcm24;
    if (s == "pcm32") return SampleFormat::Pcm32;
    return SampleFormat::Float32;
}
const char* formatName(SampleFormat f) {
    switch (f) {
    case SampleFormat::Pcm16: return "pcm16";
    case SampleFormat::Pcm24: return "pcm24";
    case SampleFormat::Pcm32: return "pcm32";
    case SampleFormat::Float32: return "float32";
    }
    return "?";
}

int genWav(const Args& a) {
    const double rate = std::stod(a.kv.at("rate"));
    const auto channels = static_cast<std::size_t>(std::stoul(a.kv.at("channels")));
    const double seconds = std::stod(a.kv.at("seconds"));
    const double freq = std::stod(a.kv.at("freq"));
    const float amp = std::stof(a.kv.at("amp"));
    const auto frames = static_cast<std::size_t>(std::llround(rate * seconds));
    radiff::core::AudioBuffer buf(channels, frames);
    for (std::size_t c = 0; c < channels; ++c)
        for (std::size_t i = 0; i < frames; ++i)
            buf.channel(c)[i] = amp * static_cast<float>(std::sin(2.0 * std::numbers::pi * freq * static_cast<double>(i) / rate));
    const auto path = radiff::platform::pathFromUtf8(a.kv.at("out"));
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    auto e = radiff::audioio::writeWav(path, rate, formatFrom(a.kv.at("format")), buf);
    return e.ok() ? 0 : die(e.message);
}

int genPreset(const Args& a) {
    radiff::presets::Preset p;
    p.name = a.kv.count("name") ? a.kv.at("name") : "Test";
    p.description = a.kv.count("description") ? a.kv.at("description") : "";
    p.author = a.kv.count("author") ? a.kv.at("author") : "";
    for (const auto& s : a.sets) {
        const auto eq = s.find('=');
        const auto id = radiff::core::paramIdForPath(s.substr(0, eq));
        if (!id)
            return die("unknown param " + s);
        const auto v = radiff::core::parseParamValue(*id, s.substr(eq + 1));
        if (!v)
            return die("bad value " + s);
        p.parameters.set(*id, *v);
    }
    auto r = radiff::presets::savePresetFile(radiff::platform::pathFromUtf8(a.kv.at("out")), p);
    return r.ok() ? 0 : die(r.error);
}

int verifyWav(const Args& a) {
    radiff::audioio::WavInfo info;
    radiff::core::AudioBuffer buf;
    const auto path = radiff::platform::pathFromUtf8(a.kv.at("file"));
    if (auto e = radiff::audioio::readWav(path, info, buf); !e.ok())
        return die(e.message);
    if (a.kv.count("rate") && std::stod(a.kv.at("rate")) != info.sampleRate)
        return die("sample rate " + std::to_string(info.sampleRate));
    if (a.kv.count("channels") && std::stoul(a.kv.at("channels")) != info.numChannels)
        return die("channels " + std::to_string(info.numChannels));
    if (a.kv.count("frames") && std::stoull(a.kv.at("frames")) != info.numFrames)
        return die("frames " + std::to_string(info.numFrames));
    if (a.kv.count("format") && a.kv.at("format") != formatName(info.format))
        return die(std::string("format ") + formatName(info.format));
    if (a.kv.count("peak-db")) {
        const std::size_t from = a.kv.count("from-seconds")
                                     ? static_cast<std::size_t>(std::stod(a.kv.at("from-seconds")) * info.sampleRate)
                                     : 0;
        float peak = 0.0f;
        for (std::size_t c = 0; c < buf.numChannels(); ++c)
            for (std::size_t i = from; i < buf.numFrames(); ++i)
                peak = std::max(peak, std::fabs(buf.channel(c)[i]));
        const double peakDb = peak > 0.0f ? 20.0 * std::log10(static_cast<double>(peak)) : -200.0;
        const double want = std::stod(a.kv.at("peak-db"));
        const double tol = a.kv.count("tol-db") ? std::stod(a.kv.at("tol-db")) : 0.1;
        if (std::fabs(peakDb - want) > tol)
            return die("peak " + std::to_string(peakDb) + " dBFS, expected " + std::to_string(want));
    }
    std::printf("ok: %s %gHz %uch %llu frames %s\n", a.kv.at("file").c_str(), info.sampleRate, info.numChannels,
                static_cast<unsigned long long>(info.numFrames), formatName(info.format));
    return 0;
}

int compareWav(const Args& a) {
    radiff::audioio::WavInfo ia, ib;
    radiff::core::AudioBuffer ba, bb;
    if (auto e = radiff::audioio::readWav(radiff::platform::pathFromUtf8(a.kv.at("a")), ia, ba); !e.ok())
        return die(e.message);
    if (auto e = radiff::audioio::readWav(radiff::platform::pathFromUtf8(a.kv.at("b")), ib, bb); !e.ok())
        return die(e.message);
    if (ia.numChannels != ib.numChannels || ia.numFrames != ib.numFrames || ia.sampleRate != ib.sampleRate)
        return die("layout differs");
    const float tol = std::stof(a.kv.at("tol"));
    float maxDiff = 0.0f;
    for (std::size_t c = 0; c < ba.numChannels(); ++c)
        for (std::size_t i = 0; i < ba.numFrames(); ++i)
            maxDiff = std::max(maxDiff, std::fabs(ba.channel(c)[i] - bb.channel(c)[i]));
    std::printf("max abs diff %g (tol %g)\n", static_cast<double>(maxDiff), static_cast<double>(tol));
    return maxDiff <= tol ? 0 : die("files differ beyond tolerance");
}

int expectExit(const std::vector<std::string>& all) {
    if (all.size() < 4)
        return die("expect-exit <code> -- <command>...");
    const int want = std::stoi(all[2]);
    const Args a = parse(all, 3);
    if (a.rest.empty())
        return die("missing command after --");
    const int got = radiff::platform::runProcess(a.rest);
    std::printf("exit code %d (expected %d)\n", got, want);
    return got == want ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    radiff::platform::prepareConsoleUtf8();
    const auto all = radiff::platform::commandLineUtf8(argc, argv);
    if (all.size() < 2)
        return die("missing subcommand");
    try {
        const std::string cmd = all[1];
        if (cmd == "gen-wav") return genWav(parse(all, 2));
        if (cmd == "gen-preset") return genPreset(parse(all, 2));
        if (cmd == "verify-wav") return verifyWav(parse(all, 2));
        if (cmd == "compare-wav") return compareWav(parse(all, 2));
        if (cmd == "expect-exit") return expectExit(all);
        return die("unknown subcommand " + cmd);
    } catch (const std::exception& e) {
        return die(e.what());
    }
}

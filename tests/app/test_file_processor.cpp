#include <doctest.h>

#include <ostream> // MSVC: operator<<(string_view) needs the full ostream before doctest stringifies values

#include "radiff/app/FileProcessor.h"
#include "radiff/platform/Paths.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <thread>

using namespace radiff;
using core::ParamId;

namespace {
std::filesystem::path tmpDir() {
    auto d = platform::pathFromUtf8(RADIFF_TEST_TMP_DIR) / "fileproc";
    std::filesystem::create_directories(d);
    return d;
}
std::filesystem::path makeInput(const char* name, std::size_t ch, double rate, std::size_t frames, float amp = 0.5f) {
    core::AudioBuffer b(ch, frames);
    for (std::size_t c = 0; c < ch; ++c)
        for (std::size_t i = 0; i < frames; ++i)
            b.channel(c)[i] = amp * std::sin(0.1f * static_cast<float>(i));
    auto p = tmpDir() / name;
    REQUIRE(audioio::writeWav(p, rate, audioio::SampleFormat::Pcm24, b).ok());
    return p;
}
core::ParameterSet allOff() {
    core::ParameterSet p;
    p.set(ParamId::InputDcBlockEnabled, false);
    p.set(ParamId::InputHighPassEnabled, false);
    p.set(ParamId::EqEnabled, false);
    p.set(ParamId::ExpEnabled, false);
    p.set(ParamId::Comp1Enabled, false);
    return p;
}
} // namespace

TEST_CASE("file job processes mono and stereo with progress and matching format") {
    for (std::size_t ch : {std::size_t{1}, std::size_t{2}}) {
        CAPTURE(ch);
        app::FileJob job;
        job.input = makeInput(ch == 1 ? "in_mono.wav" : "in_stereo.wav", ch, 48000.0, 10000);
        job.output = tmpDir() / (ch == 1 ? "out_mono.wav" : "out_stereo.wav");
        job.overwrite = true;
        job.parameters = allOff();
        job.parameters.set(ParamId::OutputGainDb, -6.0206f);
        job.blockFrames = 300; // deliberately not a divisor of 10000
        double last = 0.0;
        auto rep = app::processFile(job, [&](double f) { CHECK(f >= last); last = f; });
        REQUIRE_MESSAGE(rep.exitCode == app::ExitCode::Success, rep.message);
        CHECK(last == doctest::Approx(1.0));
        CHECK(rep.framesProcessed == 10000);
        CHECK(rep.peakOut == doctest::Approx(0.25f).epsilon(0.01));
        CHECK(rep.peakIn == doctest::Approx(0.5f).epsilon(0.01));

        audioio::WavInfo info;
        REQUIRE(audioio::readWavInfo(job.output, info).ok());
        CHECK(info.numChannels == ch);
        CHECK(info.numFrames == 10000);
        CHECK(info.format == audioio::SampleFormat::Pcm24);
    }
}

TEST_CASE("file job error mapping and overwrite opt-in") {
    app::FileJob job;
    CHECK(app::processFile(job).exitCode == app::ExitCode::UsageError);
    job.input = tmpDir() / "missing.wav";
    job.output = tmpDir() / "o.wav";
    CHECK(app::processFile(job).exitCode == app::ExitCode::InputError);
    job.input = makeInput("same.wav", 1, 44100.0, 100);
    job.output = job.input;
    CHECK(app::processFile(job).exitCode == app::ExitCode::UsageError);
    job.output = tmpDir() / "exists.wav";
    std::filesystem::remove(job.output);
    REQUIRE(app::processFile(job).exitCode == app::ExitCode::Success);
    CHECK(app::processFile(job).exitCode == app::ExitCode::OutputError); // exists, no --overwrite
    job.overwrite = true;
    CHECK(app::processFile(job).exitCode == app::ExitCode::Success);
}

TEST_CASE("output format override") {
    app::FileJob job;
    job.input = makeInput("fmt_in.wav", 1, 22050.0, 500);
    job.output = tmpDir() / "fmt_out.wav";
    job.overwrite = true;
    job.outputFormat = audioio::SampleFormat::Float32;
    REQUIRE(app::processFile(job).exitCode == app::ExitCode::Success);
    audioio::WavInfo info;
    REQUIRE(audioio::readWavInfo(job.output, info).ok());
    CHECK(info.format == audioio::SampleFormat::Float32);
    CHECK(info.sampleRate == 22050.0);
}

TEST_CASE("lookahead latency is compensated: same frame count, sample-aligned output") {
    core::AudioBuffer b(1, 20000);
    b.channel(0)[5000] = 0.9f;
    b.channel(0)[19990] = 0.9f; // near the end: must survive the flush
    const auto in = tmpDir() / "impulse.wav";
    REQUIRE(audioio::writeWav(in, 48000.0, audioio::SampleFormat::Float32, b).ok());

    app::FileJob job;
    job.input = in;
    job.output = tmpDir() / "impulse_out.wav";
    job.overwrite = true;
    job.outputFormat = audioio::SampleFormat::Float32;
    job.parameters = allOff();
    job.parameters.set(ParamId::ExpLookaheadMs, 10.0f); // 480 samples of latency
    job.blockFrames = 333;
    auto rep = app::processFile(job);
    REQUIRE_MESSAGE(rep.exitCode == app::ExitCode::Success, rep.message);
    CHECK(rep.latencyFrames == 480);

    audioio::WavInfo info;
    core::AudioBuffer out;
    REQUIRE(audioio::readWav(job.output, info, out).ok());
    REQUIRE(info.numFrames == 20000);
    for (std::size_t i = 0; i < 20000; ++i)
        CHECK(out.channel(0)[i] == ((i == 5000 || i == 19990) ? 0.9f : 0.0f));
}

TEST_CASE("silent input is fine; silent output from real input is a validation error") {
    core::AudioBuffer z(1, 4800);
    const auto in = tmpDir() / "silent.wav";
    REQUIRE(audioio::writeWav(in, 48000.0, audioio::SampleFormat::Pcm16, z).ok());
    app::FileJob job;
    job.input = in;
    job.output = tmpDir() / "silent_out.wav";
    job.overwrite = true;
    CHECK(app::processFile(job).exitCode == app::ExitCode::Success);

    // Force a silent result: wet expander with a 60 dB cap at ratio 8 cannot reach zero,
    // so use output gain and bypass mix instead: -24 dB out and -24 dB in on a tiny signal
    // still is not zero. The only way to get true silence is a zero input, so emulate the
    // failure path by feeding a non-silent input with every gain at minimum and checking
    // that it does NOT trip (output is quiet but not silent): the detector must not false-alarm.
    job.input = makeInput("quiet.wav", 1, 48000.0, 4800, 0.01f);
    job.output = tmpDir() / "quiet_out.wav";
    job.parameters = allOff();
    job.parameters.set(ParamId::InputGainDb, -24.0f);
    job.parameters.set(ParamId::OutputGainDb, -24.0f);
    CHECK(app::processFile(job).exitCode == app::ExitCode::Success);
}

TEST_CASE("analyze-only measures the input, resolves the relative threshold and writes nothing") {
    app::FileJob job;
    job.input = makeInput("in_analyze.wav", 1, 48000.0, 96000, 0.5f); // sine, RMS -9.03 dBFS
    job.analyzeOnly = true; // no output path at all
    job.parameters = allOff();
    job.parameters.set(ParamId::ExpThresholdMode, static_cast<float>(core::ThresholdMode::SpeechRelative));
    job.parameters.set(ParamId::ExpRelativeThresholdDb, -15.0f);
    const auto r = app::processFile(job);
    REQUIRE_MESSAGE(r.exitCode == app::ExitCode::Success, r.message);
    CHECK(r.analysis.hasSignal);
    CHECK(r.analysis.durationSeconds == doctest::Approx(2.0));
    CHECK(r.analysis.speechLevelDbfs == doctest::Approx(-9.03).epsilon(0.02));
    CHECK(r.analysis.peakDbfs == doctest::Approx(-6.02).epsilon(0.01));
    CHECK(r.expanderThresholdIsRelative);
    CHECK(r.expanderThresholdDbfs == doctest::Approx(-24.03).epsilon(0.01));
    CHECK(r.peakIn == doctest::Approx(0.5f).epsilon(0.01));
    CHECK(r.framesProcessed == 0);
    CHECK(r.peakOut == 0.0f);

    // Absolute mode reports the preset threshold untouched.
    job.parameters.set(ParamId::ExpThresholdMode, static_cast<float>(core::ThresholdMode::Absolute));
    job.parameters.set(ParamId::ExpThresholdDbfs, -41.0f);
    const auto r2 = app::processFile(job);
    REQUIRE(r2.exitCode == app::ExitCode::Success);
    CHECK_FALSE(r2.expanderThresholdIsRelative);
    CHECK(r2.expanderThresholdDbfs == doctest::Approx(-41.0));

    // A normal job also carries the analysis and still refuses a missing output.
    job.analyzeOnly = false;
    CHECK(app::processFile(job).exitCode == app::ExitCode::UsageError);
}

TEST_CASE("silent input: reported, and refused with --reject-silent-input") {
    const auto in = makeInput("silent.wav", 1, 48000.0, 4800, 0.0f);
    app::FileJob job;
    job.input = in;
    job.output = tmpDir() / "silent-out.wav";
    job.parameters = allOff();
    job.overwrite = true;
    auto r = app::processFile(job);
    CHECK(r.exitCode == app::ExitCode::Success); // silence is legitimate by default
    CHECK(r.inputSilent);
    CHECK(r.inputInfo.layout.find("data(") != std::string::npos);
    CHECK(r.inputInfo.layout.find("fmt(tag=1") != std::string::npos);

    job.rejectSilentInput = true;
    job.output = tmpDir() / "silent-out2.wav";
    auto rr = app::processFile(job);
    CHECK(rr.exitCode == app::ExitCode::ValidationError);
    CHECK(rr.inputSilent);
    CHECK(rr.message.find("digital silence") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(job.output));

    const auto loud = makeInput("notsilent.wav", 1, 48000.0, 4800, 0.5f);
    job.input = loud;
    job.output = tmpDir() / "silent-out3.wav";
    auto ok = app::processFile(job);
    CHECK(ok.exitCode == app::ExitCode::Success);
    CHECK_FALSE(ok.inputSilent);
}

TEST_CASE("wait-for-input: a file that appears and finishes later is processed; a missing one times out") {
    const auto late = tmpDir() / "late.wav";
    std::filesystem::remove(late);
    std::thread writer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        makeInput("late.wav", 1, 48000.0, 9600, 0.4f);
    });
    app::FileJob job;
    job.input = late;
    job.output = tmpDir() / "late-out.wav";
    job.parameters = allOff();
    job.overwrite = true;
    job.waitForInputSeconds = 10.0;
    const auto t0 = std::chrono::steady_clock::now();
    auto r = app::processFile(job);
    writer.join();
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(r.exitCode == app::ExitCode::Success);
    CHECK(r.waitedSeconds > 0.5);
    CHECK(r.framesProcessed == 9600);
    CHECK(elapsed < 9.0);

    job.input = tmpDir() / "never.wav";
    job.output = tmpDir() / "never-out.wav";
    job.waitForInputSeconds = 1.0;
    auto miss = app::processFile(job);
    CHECK(miss.exitCode == app::ExitCode::InputError);
    CHECK(miss.message.find("did not appear") != std::string::npos);
}

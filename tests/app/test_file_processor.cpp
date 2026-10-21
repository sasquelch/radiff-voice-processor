#include <doctest.h>

#include "radiff/app/FileProcessor.h"
#include "radiff/platform/Paths.h"

#include <cmath>
#include <filesystem>

using namespace radiff;

namespace {
std::filesystem::path tmpDir() {
    auto d = platform::pathFromUtf8(RADIFF_TEST_TMP_DIR) / "fileproc";
    std::filesystem::create_directories(d);
    return d;
}
std::filesystem::path makeInput(const char* name, std::size_t ch, double rate, std::size_t frames) {
    core::AudioBuffer b(ch, frames);
    for (std::size_t c = 0; c < ch; ++c)
        for (std::size_t i = 0; i < frames; ++i)
            b.channel(c)[i] = 0.5f * std::sin(0.01f * static_cast<float>(i));
    auto p = tmpDir() / name;
    REQUIRE(audioio::writeWav(p, rate, audioio::SampleFormat::Pcm24, b).ok());
    return p;
}
} // namespace

TEST_CASE("file job processes mono and stereo with progress and matching format") {
    for (std::size_t ch : {std::size_t{1}, std::size_t{2}}) {
        CAPTURE(ch);
        app::FileJob job;
        job.input = makeInput(ch == 1 ? "in_mono.wav" : "in_stereo.wav", ch, 48000.0, 10000);
        job.output = tmpDir() / (ch == 1 ? "out_mono.wav" : "out_stereo.wav");
        job.parameters.set(core::ParamId::OutputGainDb, -6.0206f);
        job.blockFrames = 300; // deliberately not a divisor of 10000
        double last = 0.0;
        auto rep = app::processFile(job, [&](double f) { CHECK(f >= last); last = f; });
        REQUIRE_MESSAGE(rep.exitCode == app::ExitCode::Success, rep.message);
        CHECK(last == doctest::Approx(1.0));
        CHECK(rep.framesProcessed == 10000);
        CHECK(rep.peakOut == doctest::Approx(0.25f).epsilon(0.01));

        audioio::WavInfo info;
        REQUIRE(audioio::readWavInfo(job.output, info).ok());
        CHECK(info.numChannels == ch);
        CHECK(info.numFrames == 10000);
        CHECK(info.format == audioio::SampleFormat::Pcm24);
    }
}

TEST_CASE("file job error mapping") {
    app::FileJob job;
    CHECK(app::processFile(job).exitCode == app::ExitCode::UsageError);
    job.input = tmpDir() / "missing.wav";
    job.output = tmpDir() / "o.wav";
    CHECK(app::processFile(job).exitCode == app::ExitCode::InputError);
    job.input = makeInput("same.wav", 1, 44100.0, 100);
    job.output = job.input;
    CHECK(app::processFile(job).exitCode == app::ExitCode::UsageError);
    job.output = tmpDir() / "exists.wav";
    REQUIRE(app::processFile(job).exitCode == app::ExitCode::Success);
    job.overwrite = false;
    CHECK(app::processFile(job).exitCode == app::ExitCode::OutputError);
}

TEST_CASE("output format override") {
    app::FileJob job;
    job.input = makeInput("fmt_in.wav", 1, 22050.0, 500);
    job.output = tmpDir() / "fmt_out.wav";
    job.outputFormat = audioio::SampleFormat::Float32;
    REQUIRE(app::processFile(job).exitCode == app::ExitCode::Success);
    audioio::WavInfo info;
    REQUIRE(audioio::readWavInfo(job.output, info).ok());
    CHECK(info.format == audioio::SampleFormat::Float32);
    CHECK(info.sampleRate == 22050.0);
}

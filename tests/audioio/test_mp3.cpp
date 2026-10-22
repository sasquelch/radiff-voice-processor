#include <doctest.h>

#include <ostream> // MSVC: operator<<(string_view) needs the full ostream before doctest stringifies values

#include "radiff/audioio/AudioFile.h"
#include "radiff/platform/Paths.h"

#include <cmath>
#include <filesystem>
#include <fstream>

using namespace radiff;

namespace {
std::filesystem::path dataDir() { return platform::pathFromUtf8(RADIFF_TEST_DATA_DIR); }
std::filesystem::path scratch() {
    auto d = platform::pathFromUtf8(RADIFF_TEST_TMP_DIR) / "audioio";
    std::filesystem::create_directories(d);
    return d;
}
double rmsDb(const core::AudioBuffer& b, std::size_t ch, std::size_t from, std::size_t to) {
    double acc = 0.0;
    for (std::size_t i = from; i < to; ++i)
        acc += static_cast<double>(b.channel(ch)[i]) * static_cast<double>(b.channel(ch)[i]);
    return 10.0 * std::log10(acc / static_cast<double>(to - from) + 1e-30);
}
/// RMS-equivalent level of the tone at hz over [from, to) by DFT correlation.
double toneDb(const core::AudioBuffer& b, std::size_t ch, std::size_t from, std::size_t to, double fs, double hz) {
    double re = 0.0, im = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        const double w = 2.0 * 3.14159265358979323846 * hz * static_cast<double>(i) / fs;
        re += static_cast<double>(b.channel(ch)[i]) * std::cos(w);
        im -= static_cast<double>(b.channel(ch)[i]) * std::sin(w);
    }
    const double peak = 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(to - from);
    return 20.0 * std::log10(peak / std::sqrt(2.0) + 1e-30);
}
} // namespace

TEST_CASE("MP3 input: a 48 kHz mono 440 Hz tone at -20 dBFS decodes to the right rate, length and level") {
    audioio::WavInfo info;
    core::AudioBuffer b;
    auto e = audioio::readAudio(dataDir() / "tone-440-1s-48k-mono.mp3", info, b);
    REQUIRE_MESSAGE(e.ok(), e.message);
    CHECK(info.container == audioio::Container::Mp3);
    CHECK(info.format == audioio::SampleFormat::Pcm24);
    CHECK(info.sampleRate == 48000.0);
    CHECK(info.numChannels == 1);
    // 1.000 s file; the LAME tag lets minimp3 strip encoder delay/padding, allow one frame of slack.
    CHECK(info.numFrames >= 48000 - 1152);
    CHECK(info.numFrames <= 48000 + 1152);
    CHECK(b.numFrames() == info.numFrames);
    // Middle 0.5 s (steady state). The source tone was -20.00 dBFS RMS; the LAME encode comes
    // out at -20.445 dBFS (ffmpeg's decoder measures the same file at -20.444963), so the
    // reference is ffmpeg's number: minimp3 must agree within 0.05 dB, and all of the energy
    // must still be at 440 Hz.
    const std::size_t from = 12000, to = 36000; // 24000 frames = 220 cycles of 440 Hz
    CHECK(rmsDb(b, 0, from, to) == doctest::Approx(-20.445).epsilon(0.0025));
    CHECK(toneDb(b, 0, from, to, 48000.0, 440.0) == doctest::Approx(-20.445).epsilon(0.0025));
    for (float v : b.channel(0))
        REQUIRE(std::isfinite(v));
}

TEST_CASE("MP3 input: 44.1 kHz stereo file, both channels identical") {
    audioio::WavInfo info;
    core::AudioBuffer b;
    auto e = audioio::readMp3(dataDir() / "tone-1k-05s-44k-stereo.mp3", info, b);
    REQUIRE_MESSAGE(e.ok(), e.message);
    CHECK(info.sampleRate == 44100.0);
    CHECK(info.numChannels == 2);
    CHECK(info.numFrames >= 22050 - 1152);
    CHECK(info.numFrames <= 22050 + 1152);
    double maxDiff = 0.0;
    for (std::size_t i = 0; i < b.numFrames(); ++i)
        maxDiff = std::max(maxDiff, std::fabs(static_cast<double>(b.channel(0)[i]) - static_cast<double>(b.channel(1)[i])));
    CHECK(maxDiff < 1e-3); // joint-stereo coding of identical channels
    CHECK(rmsDb(b, 0, 5000, 20000) == doctest::Approx(-12.265).epsilon(0.004)); // ffmpeg: -12.264651
}

TEST_CASE("readAudio detects the container from the content, not just the name") {
    // An MP3 renamed to .wav still loads as MP3; a WAV renamed to .mp3 still loads as WAV.
    const auto mp3AsWav = scratch() / "renamed-mp3.wav";
    std::filesystem::copy_file(dataDir() / "tone-440-1s-48k-mono.mp3", mp3AsWav, std::filesystem::copy_options::overwrite_existing);
    audioio::WavInfo info;
    core::AudioBuffer b;
    REQUIRE(audioio::readAudio(mp3AsWav, info, b).ok());
    CHECK(info.container == audioio::Container::Mp3);
    CHECK(info.sampleRate == 48000.0);

    core::AudioBuffer ramp(1, 480);
    for (std::size_t i = 0; i < 480; ++i)
        ramp.channel(0)[i] = static_cast<float>(i) / 480.0f;
    const auto wavAsMp3 = scratch() / "renamed-wav.mp3";
    REQUIRE(audioio::writeWav(wavAsMp3, 48000.0, audioio::SampleFormat::Float32, ramp).ok());
    core::AudioBuffer r;
    REQUIRE(audioio::readAudio(wavAsMp3, info, r).ok());
    CHECK(info.container == audioio::Container::Wav);
    CHECK(r.numFrames() == 480);
    CHECK(audioio::looksLikeMp3(wavAsMp3)); // by extension only; readAudio trusted the RIFF header

    // Garbage is rejected with a clear message.
    const auto junk = scratch() / "junk.bin";
    { std::ofstream(junk, std::ios::binary) << "this is not audio at all, just text"; }
    auto e = audioio::readAudio(junk, info, r);
    CHECK_FALSE(e.ok());
    CHECK(e.message.find("Unsupported input") != std::string::npos);
    CHECK_FALSE(audioio::readAudio(scratch() / "missing.mp3", info, r).ok());
}

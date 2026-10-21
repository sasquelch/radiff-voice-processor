#include <doctest.h>

#include "radiff/audioio/WavFile.h"
#include "radiff/ui/FilePlayer.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <vector>

using namespace radiff;
namespace fs = std::filesystem;

namespace {

/// Sine at `hz` on channel 0 and at 2*hz on channel 1 (so stereo files are distinguishable).
core::AudioBuffer tone(std::size_t channels, std::size_t frames, double fs, double hz, double rmsDb) {
    core::AudioBuffer b(channels, frames);
    const double amp = std::pow(10.0, rmsDb / 20.0) * std::sqrt(2.0);
    for (std::size_t c = 0; c < channels; ++c)
        for (std::size_t i = 0; i < frames; ++i)
            b.channel(c)[i] = static_cast<float>(
                amp * std::sin(2.0 * std::numbers::pi * hz * (1.0 + static_cast<double>(c)) * static_cast<double>(i) / fs));
    return b;
}

fs::path writeTone(const char* name, std::size_t channels, double fs, double hz, double rmsDb, double seconds) {
    const fs::path dir = fs::path(RADIFF_TEST_TMP_DIR) / "ui-player";
    fs::create_directories(dir);
    const fs::path file = dir / name;
    auto b = tone(channels, static_cast<std::size_t>(fs * seconds), fs, hz, rmsDb);
    REQUIRE(audioio::writeWav(file, fs, audioio::SampleFormat::Float32, b).ok());
    return file;
}

struct Out {
    std::vector<float> l, r;
    std::array<float*, 2> ptr{};
    explicit Out(std::size_t n) : l(n), r(n) { ptr = {l.data(), r.data()}; }
};

double rmsDb(const std::vector<float>& v, std::size_t a, std::size_t b) {
    double acc = 0.0;
    for (std::size_t i = a; i < b; ++i)
        acc += static_cast<double>(v[i]) * static_cast<double>(v[i]);
    return 10.0 * std::log10(acc / static_cast<double>(b - a) + 1e-30);
}

/// Renders `seconds` at `deviceRate` in blocks of `block` frames into one long buffer.
Out renderAll(ui::FilePlayer& p, double deviceRate, double seconds, std::size_t block) {
    const std::size_t total = static_cast<std::size_t>(deviceRate * seconds);
    Out all(total);
    Out blk(block);
    for (std::size_t pos = 0; pos < total; pos += block) {
        const std::size_t n = std::min(block, total - pos);
        p.render(blk.ptr.data(), 2, n, deviceRate);
        for (std::size_t i = 0; i < n; ++i) {
            all.l[pos + i] = blk.l[i];
            all.r[pos + i] = blk.r[i];
        }
    }
    return all;
}

} // namespace

TEST_CASE("file player: same-rate playback is bit-exact, stereo stays stereo") {
    const auto file = writeTone("stereo48.wav", 2, 48000.0, 440.0, -12.0, 0.5);
    ui::FilePlayer p;
    REQUIRE(p.load(file).empty());
    CHECK(p.hasFile());
    CHECK(p.info().sampleRate == 48000.0);
    CHECK(p.info().numChannels == 2);
    CHECK(p.durationSeconds() == doctest::Approx(0.5));
    CHECK_FALSE(p.isPlaying());
    p.play();
    CHECK(p.isPlaying());
    auto out = renderAll(p, 48000.0, 0.5, 480);
    auto ref = tone(2, 24000, 48000.0, 440.0, -12.0);
    for (std::size_t i = 0; i < 24000; ++i) {
        REQUIRE(out.l[i] == ref.channel(0)[i]);
        REQUIRE(out.r[i] == ref.channel(1)[i]);
    }
    CHECK(out.l[100] != out.r[100]); // 440 Hz vs 880 Hz differ here (they coincide at some indices)
}

TEST_CASE("file player: 48 kHz -> 44.1 kHz keeps tone levels within 0.2 dB, mono feeds both channels") {
    for (double hz : {1000.0, 6000.0}) {
        const auto file = writeTone(hz > 2000 ? "mono48_6k.wav" : "mono48_1k.wav", 1, 48000.0, hz, -20.0, 1.0);
        ui::FilePlayer p;
        REQUIRE(p.load(file).empty());
        p.play();
        auto out = renderAll(p, 44100.0, 1.0, 441);
        const std::size_t a = 11025, b = 33075; // middle half second
        CHECK(rmsDb(out.l, a, b) == doctest::Approx(-20.0).epsilon(0.01)); // 0.2 dB
        for (std::size_t i = 0; i < out.l.size(); ++i) {
            REQUIRE(std::isfinite(out.l[i]));
            REQUIRE(out.l[i] == out.r[i]);
        }
        CHECK(p.isPlaying()); // 1 s of 44.1 kHz output consumes ~0.919 s of the 1 s file
    }
}

TEST_CASE("file player: position, seek, pause, stop, loop and end-of-file") {
    const auto file = writeTone("mono48_short.wav", 1, 48000.0, 500.0, -20.0, 0.1); // 4800 frames
    ui::FilePlayer p;
    REQUIRE(p.load(file).empty());
    Out blk(480);

    SUBCASE("position advances by frames/deviceRate per block") {
        p.play();
        for (int k = 1; k <= 5; ++k) {
            p.render(blk.ptr.data(), 2, 441, 44100.0);
            CHECK(p.positionSeconds() == doctest::Approx(k * 441.0 / 44100.0).epsilon(1e-9));
        }
    }
    SUBCASE("seek while idle applies immediately; while playing on the next render") {
        p.seek(0.5);
        CHECK(p.positionSeconds() == doctest::Approx(0.05));
        p.play();
        p.seek(0.25);
        p.render(blk.ptr.data(), 2, 480, 48000.0);
        CHECK(p.positionSeconds() == doctest::Approx(0.025 + 0.01));
    }
    SUBCASE("pause outputs silence and holds the position") {
        p.play();
        p.render(blk.ptr.data(), 2, 480, 48000.0);
        const double pos = p.positionSeconds();
        p.pause();
        CHECK_FALSE(p.isPlaying());
        CHECK_FALSE(p.render(blk.ptr.data(), 2, 480, 48000.0));
        CHECK(p.positionSeconds() == doctest::Approx(pos));
        for (float v : blk.l)
            REQUIRE(v == 0.0f);
        p.togglePlay();
        CHECK(p.isPlaying());
    }
    SUBCASE("stop rewinds") {
        p.play();
        p.render(blk.ptr.data(), 2, 480, 48000.0);
        p.stop();
        CHECK_FALSE(p.isPlaying());
        CHECK(p.positionSeconds() == doctest::Approx(0.0));
    }
    SUBCASE("end of file without loop: stops, flags, outputs silence afterwards") {
        p.play();
        auto out = renderAll(p, 48000.0, 0.25, 480); // 0.1 s file -> ends inside
        CHECK_FALSE(p.isPlaying());
        CHECK(p.takeEnded());
        CHECK_FALSE(p.takeEnded()); // one-shot
        CHECK(p.positionSeconds() == doctest::Approx(0.1));
        for (std::size_t i = 4800; i < out.l.size(); ++i)
            REQUIRE(out.l[i] == 0.0f);
        CHECK(rmsDb(out.l, 0, 4800) == doctest::Approx(-20.0).epsilon(0.01));
        p.play(); // playing again from the end restarts at 0
        p.render(blk.ptr.data(), 2, 480, 48000.0);
        CHECK(p.positionSeconds() == doctest::Approx(0.01));
    }
    SUBCASE("loop wraps and keeps playing") {
        p.setLooping(true);
        p.play();
        auto out = renderAll(p, 48000.0, 0.25, 480);
        CHECK(p.isPlaying());
        CHECK(p.positionSeconds() < 0.1);
        CHECK_FALSE(p.takeEnded());
        CHECK(rmsDb(out.l, 6000, 12000) == doctest::Approx(-20.0).epsilon(0.01)); // second pass audible
    }
}

TEST_CASE("file player: loading a second file replaces the first and rewinds") {
    const auto a = writeTone("a.wav", 1, 48000.0, 500.0, -20.0, 0.2);
    const auto b = writeTone("b.wav", 2, 44100.0, 500.0, -20.0, 0.3);
    ui::FilePlayer p;
    REQUIRE(p.load(a).empty());
    p.play();
    Out blk(480);
    p.render(blk.ptr.data(), 2, 480, 48000.0);
    REQUIRE(p.load(b).empty());
    CHECK_FALSE(p.isPlaying());
    CHECK(p.positionSeconds() == doctest::Approx(0.0));
    CHECK(p.info().sampleRate == 44100.0);
    CHECK(p.info().numChannels == 2);
    CHECK(p.info().name == "b.wav");
    CHECK_FALSE(p.load(fs::path(RADIFF_TEST_TMP_DIR) / "ui-player" / "missing.wav").empty());
    CHECK(p.info().name == "b.wav"); // a failed load keeps the current file
}

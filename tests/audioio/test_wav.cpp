#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <ostream> // MSVC: operator<<(string_view) needs the full ostream before doctest stringifies values

#include "radiff/audioio/WavFile.h"
#include "radiff/platform/Paths.h"

#include <cmath>
#include <filesystem>
#include <fstream>

using namespace radiff;
using audioio::SampleFormat;

namespace {
std::filesystem::path tmpDir() {
    auto d = platform::pathFromUtf8(RADIFF_TEST_TMP_DIR) / "audioio";
    std::filesystem::create_directories(d);
    return d;
}
core::AudioBuffer ramp(std::size_t ch, std::size_t frames) {
    core::AudioBuffer b(ch, frames);
    for (std::size_t c = 0; c < ch; ++c)
        for (std::size_t i = 0; i < frames; ++i)
            b.channel(c)[i] = (static_cast<float>(i) / static_cast<float>(frames)) * 2.0f - 1.0f + 0.001f * static_cast<float>(c);
    return b;
}
} // namespace

TEST_CASE("wav round trip for every format") {
    struct Case { SampleFormat f; double tol; const char* name; };
    const Case cases[] = {{SampleFormat::Pcm16, 1.0 / 32768.0, "pcm16"},
                          {SampleFormat::Pcm24, 1.0 / 8388608.0, "pcm24"},
                          {SampleFormat::Pcm32, 1.0 / 16777216.0, "pcm32"}  /* float32 mantissa limit */,
                          {SampleFormat::Float32, 0.0, "float32"}};
    for (const auto& c : cases) {
        CAPTURE(std::string(c.name));
        auto src = ramp(2, 1000);
        const auto path = tmpDir() / (std::string("rt_") + c.name + ".wav");
        REQUIRE(audioio::writeWav(path, 48000.0, c.f, src).ok());

        audioio::WavInfo info;
        core::AudioBuffer dst;
        auto e = audioio::readWav(path, info, dst);
        REQUIRE_MESSAGE(e.ok(), e.message);
        CHECK(info.sampleRate == 48000.0);
        CHECK(info.numChannels == 2);
        CHECK(info.numFrames == 1000);
        CHECK(info.format == c.f);
        for (std::size_t ch = 0; ch < 2; ++ch)
            for (std::size_t i = 0; i < 1000; ++i)
                CHECK(std::fabs(static_cast<double>(dst.channel(ch)[i] - src.channel(ch)[i])) <= c.tol + 1e-9);
    }
}

TEST_CASE("reads WAVE_FORMAT_EXTENSIBLE and skips unknown chunks") {
    // Hand-built file: RIFF, LIST chunk (unknown), fmt (extensible, 24-bit, 1ch), data (2 frames)
    const auto path = tmpDir() / "extensible.wav";
    std::ofstream out(path, std::ios::binary);
    auto w32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) out.put(static_cast<char>((v >> (8 * i)) & 0xFF)); };
    auto w16 = [&](std::uint16_t v) { out.put(static_cast<char>(v & 0xFF)); out.put(static_cast<char>(v >> 8)); };
    out.write("RIFF", 4); w32(0); out.write("WAVE", 4);
    out.write("LIST", 4); w32(5); out.write("abcde", 5); out.put('\0'); // odd size -> pad byte
    out.write("fmt ", 4); w32(40);
    w16(0xFFFE); w16(1); w32(44100); w32(44100 * 3); w16(3); w16(24);
    w16(22); w16(24); w32(4);
    w16(0x0001); w16(0x0000); w32(0x00100000); w32(0xAA000080); w32(0x719B3800); // KSDATAFORMAT_SUBTYPE_PCM
    out.write("data", 4); w32(6);
    out.put('\0'); out.put('\0'); out.put('\x40'); // +0.5
    out.put('\0'); out.put('\0'); out.put('\xC0'); // -0.5
    out.close();

    audioio::WavInfo info;
    core::AudioBuffer buf;
    auto e = audioio::readWav(path, info, buf);
    REQUIRE_MESSAGE(e.ok(), e.message);
    CHECK(info.format == SampleFormat::Pcm24);
    CHECK(info.numChannels == 1);
    CHECK(info.numFrames == 2);
    CHECK(buf.channel(0)[0] == doctest::Approx(0.5f));
    CHECK(buf.channel(0)[1] == doctest::Approx(-0.5f));
}

TEST_CASE("rejects non-wav and missing files with messages") {
    const auto path = tmpDir() / "garbage.wav";
    { std::ofstream(path, std::ios::binary) << "this is not audio"; }
    audioio::WavInfo info;
    auto e = audioio::readWavInfo(path, info);
    CHECK_FALSE(e.ok());
    CHECK(e.message.find("RIFF") != std::string::npos);

    auto e2 = audioio::readWavInfo(tmpDir() / "nope.wav", info);
    CHECK_FALSE(e2.ok());
}

TEST_CASE("writer clips out-of-range samples instead of wrapping") {
    core::AudioBuffer b(1, 4);
    b.channel(0)[0] = 2.0f; b.channel(0)[1] = -2.0f; b.channel(0)[2] = 1.0f; b.channel(0)[3] = -1.0f;
    const auto path = tmpDir() / "clip.wav";
    REQUIRE(audioio::writeWav(path, 8000.0, SampleFormat::Pcm16, b).ok());
    audioio::WavInfo info;
    core::AudioBuffer r;
    REQUIRE(audioio::readWav(path, info, r).ok());
    CHECK(r.channel(0)[0] > 0.99f);
    CHECK(r.channel(0)[1] < -0.99f);
}

TEST_CASE("unicode file names round trip through std::filesystem") {
    const auto path = tmpDir() / platform::pathFromUtf8("ünïcödé 音声 ✓.wav");
    auto src = ramp(1, 10);
    REQUIRE(audioio::writeWav(path, 48000.0, SampleFormat::Float32, src).ok());
    CHECK(std::filesystem::exists(path));
    CHECK(platform::pathToUtf8(path.filename()) == "ünïcödé 音声 ✓.wav");
    audioio::WavInfo info;
    REQUIRE(audioio::readWavInfo(path, info).ok());
    CHECK(info.numFrames == 10);
}

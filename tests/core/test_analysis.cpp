#include <doctest.h>

#include <ostream> // MSVC: operator<<(string_view) needs the full ostream before doctest stringifies values

#include "TestSignals.h"
#include "radiff/core/Analysis.h"

#include <cmath>

using namespace radiff;

namespace {
constexpr double fs = 48000.0;

/// Speech-like test material: tone bursts at `speechRmsDb` with the given duty cycle, over a
/// continuous noise floor at `floorRmsDb`, plus optional "breaths" at `breathRmsDb` in the gaps.
core::AudioBuffer material(double speechRmsDb, double floorRmsDb, double duty, double breathRmsDb = -144.0) {
    const std::size_t frames = 48000 * 8;
    core::AudioBuffer b(1, frames);
    const double sa = std::sqrt(2.0) * std::pow(10.0, speechRmsDb / 20.0);
    const double fa = std::sqrt(2.0) * std::pow(10.0, floorRmsDb / 20.0);
    const double ba = std::sqrt(2.0) * std::pow(10.0, breathRmsDb / 20.0);
    unsigned seed = 1;
    for (std::size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / fs;
        const double phase = std::fmod(t, 1.0); // 1 s period
        seed = seed * 1664525u + 1013904223u;
        const double noise = (static_cast<double>(seed >> 8) / 8388608.0 - 1.0) * fa; // ~uniform
        double v = noise;
        if (phase < duty)
            v += sa * std::sin(2.0 * test::kPi * 200.0 * t);
        else if (phase > duty + 0.1 && phase < duty + 0.4)
            v += ba * std::sin(2.0 * test::kPi * 3000.0 * t);
        b.channel(0)[i] = static_cast<float>(v);
    }
    return b;
}

core::LevelAnalysis analyze(const core::AudioBuffer& b, std::size_t block = 512) {
    core::LevelAnalyzer a;
    a.prepare(fs, b.numChannels());
    core::AudioBuffer copy(b);
    test::processInBlocks(copy, block, [&](core::AudioView v) {
        a.process(core::ConstAudioView{v.channels, v.numChannels, v.numFrames});
    });
    return a.result();
}
} // namespace

TEST_CASE("analyzer: speech level tracks the active material, not the pauses") {
    // Same speech level, 70 % vs 30 % duty: the gated level must agree within 1 dB.
    const auto busy = analyze(material(-20.0, -65.0, 0.7));
    const auto sparse = analyze(material(-20.0, -65.0, 0.3));
    CHECK(busy.hasSignal);
    CHECK(busy.speechLevelDbfs == doctest::Approx(-20.0).epsilon(0.03));
    CHECK(sparse.speechLevelDbfs == doctest::Approx(-20.0).epsilon(0.03));
    CHECK(std::fabs(busy.speechLevelDbfs - sparse.speechLevelDbfs) < 1.0);
    CHECK(busy.activeSeconds == doctest::Approx(0.7 * 8.0).epsilon(0.05));
    CHECK(sparse.activeSeconds == doctest::Approx(0.3 * 8.0).epsilon(0.05));
    CHECK(busy.durationSeconds == doctest::Approx(8.0));
}

TEST_CASE("analyzer: breaths 20 dB under speech do not drag the speech level down") {
    const auto clean = analyze(material(-24.0, -70.0, 0.5));
    const auto breathy = analyze(material(-24.0, -70.0, 0.5, -44.0));
    CHECK(std::fabs(clean.speechLevelDbfs - breathy.speechLevelDbfs) < 0.5);
}

TEST_CASE("analyzer: noise floor and peak are reported; block size is irrelevant") {
    const auto b = material(-18.0, -55.0, 0.5);
    const auto r = analyze(b);
    CHECK(r.noiseFloorDbfs == doctest::Approx(-55.0).epsilon(0.05)); // uniform noise RMS ~ amplitude/sqrt(3)*sqrt(2)... measured
    CHECK(r.peakDbfs == doctest::Approx(-18.0 + 3.01).epsilon(0.02));
    const auto r2 = analyze(b, 37);
    CHECK(r2.speechLevelDbfs == doctest::Approx(r.speechLevelDbfs).epsilon(1e-9));
    CHECK(r2.noiseFloorDbfs == doctest::Approx(r.noiseFloorDbfs).epsilon(1e-9));
}

TEST_CASE("analyzer: digital silence reports no signal") {
    core::AudioBuffer b(2, 48000);
    const auto r = analyze(b);
    CHECK_FALSE(r.hasSignal);
    CHECK(r.durationSeconds == doctest::Approx(1.0));
    CHECK(r.peakDbfs == doctest::Approx(-144.0));
}

TEST_CASE("analyzer: stereo uses the louder channel") {
    core::AudioBuffer b = material(-30.0, -80.0, 0.6);
    core::AudioBuffer st(2, b.numFrames());
    for (std::size_t i = 0; i < b.numFrames(); ++i) {
        st.channel(0)[i] = b.channel(0)[i] * 0.1f; // -20 dB
        st.channel(1)[i] = b.channel(0)[i];
    }
    const auto r = analyze(st);
    CHECK(r.speechLevelDbfs == doctest::Approx(-30.0).epsilon(0.03));
}

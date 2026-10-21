#include <doctest.h>

#include <ostream> // MSVC: operator<<(string_view) needs the full ostream before doctest stringifies values

#include "TestSignals.h"
#include "radiff/core/modules/Presence.h"

using namespace radiff;
using core::ParamId;
using core::ParameterSet;

namespace {
constexpr double fs = 48000.0;

ParameterSet make(float lf, float hf, float dynamic, bool enabled = true) {
    ParameterSet p;
    p.set(ParamId::PresEnabled, enabled);
    p.set(ParamId::PresLfDetail, lf);
    p.set(ParamId::PresHfDetail, hf);
    p.set(ParamId::PresHfDynamic, dynamic);
    return p; // lfFrequencyHz 100, hfFrequencyHz 3500 defaults
}

core::AudioBuffer run(const ParameterSet& p, core::AudioBuffer b, core::modules::Presence* out = nullptr) {
    core::modules::Presence m;
    m.prepare(fs, b.numChannels());
    m.update(p);
    test::processInBlocks(b, 512, [&](core::AudioView v) { m.process(v); });
    if (out)
        *out = m;
    return b;
}

core::AudioBuffer twoTones(std::size_t frames, double aHz, double aDb, double bHz, double bDb) {
    auto a = test::sineRms(1, frames, fs, aHz, aDb);
    auto b = test::sineRms(1, frames, fs, bHz, bDb);
    for (std::size_t i = 0; i < frames; ++i)
        a.channel(0)[i] += b.channel(0)[i];
    return a;
}
} // namespace

TEST_CASE("LF Detail is a low shelf: 0.6 dB per step below the corner, flat above") {
    core::modules::Presence m;
    auto y = run(make(10.0f, 0.0f, 0.0f), test::sineRms(1, 48000, fs, 50.0, -20.0), &m);
    CHECK(m.lfGainDb() == doctest::Approx(6.0));
    CHECK(m.magnitudeDb(20.0) == doctest::Approx(6.0).epsilon(0.05));
    CHECK(std::fabs(m.magnitudeDb(1000.0)) < 0.1);
    CHECK(test::toneLevelDb(y, 0, 24000, 48000, fs, 50.0) ==
          doctest::Approx(-20.0 + m.magnitudeDb(50.0)).epsilon(0.01));
    auto half = run(make(5.0f, 0.0f, 0.0f), test::sineRms(1, 48000, fs, 50.0, -20.0), &m);
    CHECK(m.lfGainDb() == doctest::Approx(3.0));
}

TEST_CASE("HF Detail with dynamic 0 is a static high shelf: 0.8 dB per step") {
    core::modules::Presence m;
    auto y = run(make(0.0f, 10.0f, 0.0f), twoTones(48000, 300.0, -30.0, 12000.0, -30.0), &m);
    CHECK(m.hfStaticGainDb() == doctest::Approx(8.0));
    CHECK(m.magnitudeDb(12000.0) > 7.0);
    CHECK(std::fabs(m.magnitudeDb(300.0)) < 0.1);
    CHECK(test::toneLevelDb(y, 0, 24000, 48000, fs, 12000.0) ==
          doctest::Approx(-30.0 + m.magnitudeDb(12000.0)).epsilon(0.01));
    CHECK(test::toneLevelDb(y, 0, 24000, 48000, fs, 300.0) == doctest::Approx(-30.0).epsilon(0.005));
}

TEST_CASE("dynamic HF Detail backs off when the high band already dominates") {
    core::modules::Presence m;
    // A lone 10 kHz tone IS the high band: dominance ~0 dB -> boost scaled away.
    auto lone = run(make(0.0f, 10.0f, 1.0f), test::sineRms(1, 48000, fs, 10000.0, -20.0), &m);
    CHECK(m.hfCurrentGainDb() < 0.5);
    CHECK(std::fabs(test::toneLevelDb(lone, 0, 24000, 48000, fs, 10000.0) + 20.0) < 0.5);

    // Quiet highs under a dominant low tone (dominance -30 dB) get the full boost.
    auto mixed = run(make(0.0f, 10.0f, 1.0f), twoTones(48000, 300.0, -10.0, 10000.0, -40.0), &m);
    CHECK(m.hfCurrentGainDb() > 7.5);
    CHECK(test::toneLevelDb(mixed, 0, 24000, 48000, fs, 10000.0) ==
          doctest::Approx(-40.0 + m.magnitudeDb(10000.0)).epsilon(0.015));
    CHECK(test::toneLevelDb(mixed, 0, 24000, 48000, fs, 300.0) == doctest::Approx(-10.0).epsilon(0.01));

    // Half sensitivity: the lone tone keeps half the boost.
    run(make(0.0f, 10.0f, 0.5f), test::sineRms(1, 48000, fs, 10000.0, -20.0), &m);
    CHECK(m.hfCurrentGainDb() == doctest::Approx(4.0).epsilon(0.1));
}

TEST_CASE("enhancer makeup gain, off state and click-free enabling") {
    auto p = make(0.0f, 0.0f, 0.0f);
    p.set(ParamId::PresMakeupGainDb, -6.0f);
    auto y = run(p, test::sineRms(1, 48000, fs, 1000.0, -20.0));
    CHECK(test::rmsDb(y, 0, 24000, 48000) == doctest::Approx(-26.0).epsilon(0.002));

    auto src = test::sineRms(1, 24000, fs, 1000.0, -20.0);
    auto off = run(make(10.0f, 10.0f, 0.0f, false), src);
    CHECK(test::maxAbsDiff(src, off) == 0.0);

    // Toggle on mid-stream: no sample step larger than the tone's own.
    core::modules::Presence m;
    m.prepare(fs, 1);
    m.update(make(10.0f, 10.0f, 0.0f, false));
    auto b = test::sineRms(1, 48000, fs, 1000.0, -12.0);
    const double natural = test::maxStep(b, 0, 0, 1000);
    std::size_t pos = 0;
    test::processInBlocks(b, 480, [&](core::AudioView v) {
        if (pos == 24000)
            m.update(make(10.0f, 10.0f, 0.0f, true));
        m.process(v);
        pos += v.numFrames;
    });
    CHECK(test::maxStep(b, 0, 23000, 26000) < natural * 1.6);
    CHECK(test::allFinite(b));
}

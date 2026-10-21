#include <doctest.h>

#include <ostream> // MSVC: operator<<(string_view) needs the full ostream before doctest stringifies values

#include "TestSignals.h"
#include "radiff/core/modules/Equalizer.h"

using namespace radiff;
using core::EqBandType;
using core::ParamId;
using core::ParameterSet;

namespace {
constexpr double fs = 48000.0;
auto bandParam = [](int band, const char* key) { return ParameterSet::eqParam(band, key); };

double measure(const ParameterSet& p, double hz) {
    core::modules::Equalizer eq;
    eq.prepare(fs, 1);
    eq.update(p);
    auto b = test::sineRms(1, 96000, fs, hz, -20.0);
    test::processInBlocks(b, 480, [&](core::AudioView v) { eq.process(v); });
    return test::rmsDb(b, 0, 48000, 96000) + 20.0;
}
} // namespace

TEST_CASE("flat EQ is bit-transparent") {
    ParameterSet p; // all bands disabled by default
    core::modules::Equalizer eq;
    eq.prepare(fs, 2);
    eq.update(p);
    auto b = test::sineRms(2, 4800, fs, 440.0, -12.0);
    auto ref = b;
    eq.process(b.view());
    CHECK(test::maxAbsDiff(b, ref) == 0.0);
}

TEST_CASE("bell band boosts at centre and leaves the rest alone") {
    ParameterSet p;
    p.set(bandParam(2, "enabled"), true);
    p.set(bandParam(2, "type"), static_cast<float>(EqBandType::Bell));
    p.set(bandParam(2, "frequencyHz"), 1000.0f);
    p.set(bandParam(2, "gainDb"), 6.0f);
    p.set(bandParam(2, "q"), 1.0f);
    CHECK(measure(p, 1000.0) == doctest::Approx(6.0).epsilon(0.01));
    CHECK(std::fabs(measure(p, 100.0)) < 0.1);
    CHECK(std::fabs(measure(p, 10000.0)) < 0.1);
    p.set(bandParam(2, "gainDb"), -12.0f);
    CHECK(measure(p, 1000.0) == doctest::Approx(-12.0).epsilon(0.01));
}

TEST_CASE("shelves and pass bands") {
    ParameterSet p;
    p.set(bandParam(0, "enabled"), true); // low shelf 120 Hz (default type)
    p.set(bandParam(0, "gainDb"), -6.0f);
    CHECK(measure(p, 30.0) == doctest::Approx(-6.0).epsilon(0.03));
    CHECK(std::fabs(measure(p, 3000.0)) < 0.1);

    ParameterSet hs;
    hs.set(bandParam(4, "enabled"), true); // high shelf 10 kHz (default type)
    hs.set(bandParam(4, "gainDb"), 3.0f);
    CHECK(measure(hs, 20000.0) == doctest::Approx(3.0).epsilon(0.05));
    CHECK(std::fabs(measure(hs, 300.0)) < 0.1);

    ParameterSet lp;
    lp.set(bandParam(3, "enabled"), true);
    lp.set(bandParam(3, "type"), static_cast<float>(EqBandType::LowPass));
    lp.set(bandParam(3, "frequencyHz"), 5000.0f);
    lp.set(bandParam(3, "q"), 0.7071f);
    CHECK(measure(lp, 5000.0) == doctest::Approx(-3.0).epsilon(0.03));
    CHECK(measure(lp, 15000.0) < -18.0);

    ParameterSet hp;
    hp.set(bandParam(1, "enabled"), true);
    hp.set(bandParam(1, "type"), static_cast<float>(EqBandType::HighPass));
    hp.set(bandParam(1, "frequencyHz"), 200.0f);
    hp.set(bandParam(1, "q"), 0.7071f);
    CHECK(measure(hp, 200.0) == doctest::Approx(-3.0).epsilon(0.03));
    CHECK(measure(hp, 50.0) < -22.0);
}

TEST_CASE("bands cascade additively in dB") {
    ParameterSet p;
    for (int b : {1, 2, 3}) {
        p.set(bandParam(b, "enabled"), true);
        p.set(bandParam(b, "type"), static_cast<float>(EqBandType::Bell));
        p.set(bandParam(b, "frequencyHz"), 2000.0f);
        p.set(bandParam(b, "gainDb"), 2.0f);
        p.set(bandParam(b, "q"), 0.5f);
    }
    CHECK(measure(p, 2000.0) == doctest::Approx(6.0).epsilon(0.01));
    core::modules::Equalizer eq;
    eq.prepare(fs, 1);
    eq.update(p);
    CHECK(eq.magnitudeDb(2000.0) == doctest::Approx(6.0).epsilon(0.001));
}

TEST_CASE("frequencies above Nyquist are clamped safely at every sample rate") {
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        ParameterSet p;
        p.set(bandParam(4, "enabled"), true);
        p.set(bandParam(4, "frequencyHz"), 20000.0f);
        p.set(bandParam(4, "gainDb"), 18.0f);
        p.set(bandParam(4, "q"), 12.0f);
        core::modules::Equalizer eq;
        eq.prepare(rate, 1);
        eq.update(p);
        auto b = test::sineRms(1, 48000, rate, 1000.0, -20.0);
        b.channel(0)[0] = 1.0f;
        eq.process(b.view());
        CHECK(test::allFinite(b));
        CHECK(test::peakDb(b, 0, 0, 48000) < 30.0);
    }
}

TEST_CASE("module and band toggles crossfade without clicks") {
    core::modules::Equalizer eq;
    eq.prepare(fs, 1);
    ParameterSet p;
    eq.update(p);
    auto b = test::sineRms(1, 19200, fs, 100.0, -6.0);
    eq.process(b.view(0, 9600));
    p.set(bandParam(1, "enabled"), true);
    p.set(bandParam(1, "frequencyHz"), 100.0f);
    p.set(bandParam(1, "gainDb"), 12.0f);
    p.set(bandParam(1, "q"), 4.0f);
    eq.update(p);
    eq.process(b.view(9600, 4800));
    p.set(ParamId::EqEnabled, false);
    eq.update(p);
    eq.process(b.view(14400, 4800));
    CHECK(test::maxStep(b, 0, 9500, 19200) < 0.05); // natural step ~0.0093 at -6 dBFS; +12 dB -> ~0.037
    CHECK(test::allFinite(b));
}

#include <doctest.h>

#include <ostream> // MSVC: operator<<(string_view) needs the full ostream before doctest stringifies values

#include "TestSignals.h"
#include "radiff/core/modules/Deesser.h"

using namespace radiff;
using core::ParamId;
using core::ParameterSet;

namespace {
constexpr double fs = 48000.0;

ParameterSet on() {
    ParameterSet p;
    p.set(ParamId::DeessEnabled, true);
    p.set(ParamId::DeessFrequencyHz, 6000.0f);
    p.set(ParamId::DeessBandwidthOct, 1.0f);
    p.set(ParamId::DeessThresholdDbfs, -40.0f);
    p.set(ParamId::DeessMaxReductionDb, 6.0f);
    p.set(ParamId::DeessAttackMs, 0.5f);
    p.set(ParamId::DeessReleaseMs, 20.0f);
    return p;
}

/// Two simultaneous tones (RMS levels in dBFS).
core::AudioBuffer twoTones(std::size_t ch, std::size_t frames, double aHz, double aDb, double bHz, double bDb) {
    auto a = test::sineRms(ch, frames, fs, aHz, aDb);
    auto b = test::sineRms(ch, frames, fs, bHz, bDb);
    for (std::size_t c = 0; c < ch; ++c)
        for (std::size_t i = 0; i < frames; ++i)
            a.channel(c)[i] += b.channel(c)[i];
    return a;
}

core::AudioBuffer run(const ParameterSet& p, core::AudioBuffer b, core::modules::Deesser* out = nullptr) {
    core::modules::Deesser d;
    d.prepare(fs, b.numChannels());
    d.update(p);
    test::processInBlocks(b, 512, [&](core::AudioView v) { d.process(v); });
    if (out)
        *out = d;
    return b;
}
} // namespace

TEST_CASE("de-esser: the detection band is reduced by the cap, material outside it is untouched") {
    // 1 kHz and 6 kHz at -20 dBFS RMS each; the 6 kHz band peaks 23 dB above the threshold -> capped 6 dB.
    core::modules::Deesser d;
    auto y = run(on(), twoTones(1, 48000, 1000.0, -20.0, 6000.0, -20.0), &d);
    CHECK(test::toneLevelDb(y, 0, 24000, 48000, fs, 6000.0) == doctest::Approx(-26.0).epsilon(0.02));
    CHECK(test::toneLevelDb(y, 0, 24000, 48000, fs, 1000.0) == doctest::Approx(-20.0).epsilon(0.005));
    CHECK(d.lastGainReductionDb() == doctest::Approx(6.0).epsilon(0.02));
    CHECK(test::allFinite(y));
}

TEST_CASE("de-esser static curve: nothing below threshold, 6 dB knee, hard cap") {
    core::modules::Deesser d;
    d.prepare(fs, 1);
    d.update(on()); // threshold -40, cap 6
    CHECK(d.staticReductionDb(-50.0) == doctest::Approx(0.0));
    CHECK(d.staticReductionDb(-39.0) == doctest::Approx(16.0 / 12.0)); // inside the knee
    CHECK(d.staticReductionDb(-37.0) == doctest::Approx(3.0));         // knee edge = 1:inf line
    CHECK(d.staticReductionDb(-35.0) == doctest::Approx(5.0));
    CHECK(d.staticReductionDb(-10.0) == doctest::Approx(6.0));         // capped
}

TEST_CASE("de-esser below threshold or disabled changes nothing") {
    auto quiet = test::sineRms(1, 24000, fs, 6000.0, -50.0);
    auto y = run(on(), quiet);
    CHECK(test::maxAbsDiff(quiet, y) < 1e-6);

    auto p = on();
    p.set(ParamId::DeessEnabled, false);
    auto loud = twoTones(1, 24000, 1000.0, -20.0, 6000.0, -20.0);
    auto z = run(p, loud);
    CHECK(test::maxAbsDiff(loud, z) == 0.0);
}

TEST_CASE("de-esser attack and release follow the parameters") {
    core::modules::Deesser d;
    d.prepare(fs, 1);
    d.update(on()); // attack 0.5 ms, release 20 ms
    core::AudioBuffer b(1, 48000);
    auto tone = test::sineRms(1, 24000, fs, 6000.0, -10.0);
    for (std::size_t i = 0; i < 24000; ++i)
        b.channel(0)[i] = tone.channel(0)[i]; // tone for 0.5 s, then silence
    std::vector<float> gr;
    test::processInBlocks(b, 48, [&](core::AudioView v) { // 1 ms blocks
        d.process(v);
        gr.push_back(d.lastGainReductionDb());
    });
    CHECK(gr[5] > 5.5f);    // 5 ms after onset: within 0.5 dB of the 6 dB cap
    CHECK(gr[499] > 5.9f);  // steady
    CHECK(gr[600] < 0.1f);  // 100 ms (5 time constants) after the tone stops
}

TEST_CASE("de-esser listen mode outputs the detection band") {
    auto p = on();
    p.set(ParamId::DeessListen, true);
    core::modules::Deesser d;
    auto y = run(p, test::sineRms(1, 48000, fs, 1000.0, -20.0), &d);
    const double predicted = -20.0 + d.bandCoefficients().magnitudeDb(1000.0, fs);
    CHECK(predicted < -30.0); // 1 kHz is well down the skirt of a 6 kHz, 1-octave band
    CHECK(test::toneLevelDb(y, 0, 24000, 48000, fs, 1000.0) == doctest::Approx(predicted).epsilon(0.01));
}

TEST_CASE("de-esser stereo link applies the louder channel's reduction to both") {
    core::AudioBuffer b(2, 48000);
    auto loud = test::sineRms(1, 48000, fs, 6000.0, -20.0);
    auto soft = test::sineRms(1, 48000, fs, 6000.0, -50.0);
    for (std::size_t i = 0; i < 48000; ++i) {
        b.channel(0)[i] = loud.channel(0)[i];
        b.channel(1)[i] = soft.channel(0)[i];
    }
    auto linked = run(on(), b);
    CHECK(test::toneLevelDb(linked, 1, 24000, 48000, fs, 6000.0) == doctest::Approx(-56.0).epsilon(0.01));
    auto p = on();
    p.set(ParamId::DeessStereoLink, false);
    auto unlinked = run(p, b);
    CHECK(test::toneLevelDb(unlinked, 1, 24000, 48000, fs, 6000.0) == doctest::Approx(-50.0).epsilon(0.005));
    CHECK(test::toneLevelDb(unlinked, 0, 24000, 48000, fs, 6000.0) == doctest::Approx(-26.0).epsilon(0.02));
}

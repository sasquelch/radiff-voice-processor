#include <doctest.h>

#include <ostream> // MSVC: operator<<(string_view) needs the full ostream before doctest stringifies values

#include "TestSignals.h"
#include "radiff/core/modules/InputStage.h"

using namespace radiff;
using core::ParamId;
using core::ParameterSet;

namespace {
constexpr double fs = 48000.0;

double measure(const ParameterSet& p, double hz, std::size_t channels = 1) {
    core::modules::InputStage s;
    s.prepare(fs, channels);
    s.update(p);
    auto b = test::sineRms(channels, 96000, fs, hz, -20.0);
    test::processInBlocks(b, 512, [&](core::AudioView v) { s.process(v); });
    return test::rmsDb(b, 0, 48000, 96000) + 20.0;
}
} // namespace

TEST_CASE("input gain is exact in dB") {
    ParameterSet p;
    p.set(ParamId::InputHighPassEnabled, false);
    p.set(ParamId::InputDcBlockEnabled, false);
    p.set(ParamId::InputGainDb, -6.0f);
    CHECK(measure(p, 1000.0) == doctest::Approx(-6.0).epsilon(0.002));
    p.set(ParamId::InputGainDb, 12.0f);
    CHECK(measure(p, 1000.0) == doctest::Approx(12.0).epsilon(0.002));
}

TEST_CASE("high-pass 12 dB/oct Butterworth at 80 Hz") {
    ParameterSet p; // defaults: HPF on, 80 Hz, 12 dB/oct, DC on
    CHECK(measure(p, 80.0) == doctest::Approx(-3.0).epsilon(0.03));
    CHECK(measure(p, 40.0) == doctest::Approx(-12.3).epsilon(0.03));
    CHECK(measure(p, 20.0) == doctest::Approx(-24.1).epsilon(0.03));
    CHECK(std::fabs(measure(p, 1000.0)) < 0.05);
}

TEST_CASE("high-pass 24 dB/oct is a 4th-order Butterworth") {
    ParameterSet p;
    p.set(ParamId::InputHighPassSlopeDbOct, 24.0f);
    p.set(ParamId::InputHighPassHz, 100.0f);
    CHECK(measure(p, 100.0) == doctest::Approx(-3.0).epsilon(0.03));
    CHECK(measure(p, 50.0) == doctest::Approx(-24.1).epsilon(0.03));
    CHECK(std::fabs(measure(p, 2000.0)) < 0.05);
    // magnitudeDb() display helper agrees with the measurement.
    core::modules::InputStage s;
    s.prepare(fs, 1);
    s.update(p);
    CHECK(s.magnitudeDb(50.0) == doctest::Approx(-24.1).epsilon(0.03));
}

TEST_CASE("dc blocker removes offset without touching the voice band") {
    ParameterSet p;
    p.set(ParamId::InputHighPassEnabled, false);
    core::modules::InputStage s;
    s.prepare(fs, 1);
    s.update(p);
    auto b = test::sineRms(1, 96000, fs, 200.0, -20.0);
    for (auto& v : b.channel(0))
        v += 0.3f;
    test::processInBlocks(b, 256, [&](core::AudioView v) { s.process(v); });
    double mean = 0.0;
    for (std::size_t i = 48000; i < 96000; ++i)
        mean += static_cast<double>(b.channel(0)[i]);
    mean /= 48000.0;
    CHECK(std::fabs(mean) < 1e-3);
    CHECK(test::rmsDb(b, 0, 48000, 96000) == doctest::Approx(-20.0).epsilon(0.002));
}

TEST_CASE("toggling the high-pass mid-stream does not click") {
    core::modules::InputStage s;
    s.prepare(fs, 1);
    ParameterSet p;
    p.set(ParamId::InputHighPassEnabled, false);
    s.update(p);
    auto b = test::sineRms(1, 19200, fs, 150.0, -6.0);
    s.process(b.view(0, 9600));
    p.set(ParamId::InputHighPassEnabled, true);
    s.update(p);
    s.process(b.view(9600, 9600));
    CHECK(test::maxStep(b, 0, 9500, 10000) < 0.03); // natural step of this sine is ~0.014
    CHECK(test::allFinite(b));
}

TEST_CASE("stereo channels are processed identically and independently") {
    ParameterSet p;
    core::modules::InputStage s;
    s.prepare(fs, 2);
    s.update(p);
    core::AudioBuffer b(2, 4800);
    for (std::size_t i = 0; i < 4800; ++i) {
        b.channel(0)[i] = static_cast<float>(0.5 * std::sin(2 * test::kPi * 300.0 * static_cast<double>(i) / fs));
        b.channel(1)[i] = 0.0f;
    }
    s.process(b.view());
    CHECK(test::peakDb(b, 1, 0, 4800) < -200.0); // nothing bleeds into the silent channel
    CHECK(test::rmsDb(b, 0, 2400, 4800) == doctest::Approx(-9.03).epsilon(0.01));
}

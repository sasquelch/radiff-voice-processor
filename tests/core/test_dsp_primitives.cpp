#include <doctest.h>

#include "TestSignals.h"
#include "radiff/core/dsp/Biquad.h"
#include "radiff/core/dsp/DcBlocker.h"
#include "radiff/core/dsp/DelayLine.h"
#include "radiff/core/dsp/Detectors.h"
#include "radiff/core/dsp/GainComputer.h"
#include "radiff/core/dsp/Units.h"

using namespace radiff;
using namespace radiff::core::dsp;

namespace {
constexpr double fs = 48000.0;

/// Measures the steady-state gain of a biquad at `hz` by processing a sine.
double measureDb(const BiquadCoeffs& c, double hz) {
    Biquad f;
    f.prepare(fs);
    f.setCoefficients(c, true);
    auto b = test::sineRms(1, 48000, fs, hz, -20.0);
    f.process(b.channel(0).data(), b.numFrames());
    return test::rmsDb(b, 0, 24000, 48000) + 20.0;
}
} // namespace

TEST_CASE("dB conversions") {
    CHECK(dbToGain(0.0) == doctest::Approx(1.0));
    CHECK(dbToGain(-6.0206) == doctest::Approx(0.5).epsilon(1e-4));
    CHECK(gainToDb(2.0) == doctest::Approx(6.0206).epsilon(1e-4));
    CHECK(gainToDb(0.0) == kMinusInfDb);
    CHECK(timeConstantCoefficient(0.0, fs) == 0.0);
    // After one time constant a step reaches 63.2%.
    const double k = timeConstantCoefficient(10.0, fs);
    double y = 0.0;
    for (int i = 0; i < 480; ++i)
        y = 1.0 + k * (y - 1.0);
    CHECK(y == doctest::Approx(0.632).epsilon(0.01));
}

TEST_CASE("biquad designs match the analytic response and the textbook points") {
    const auto lp = design::lowPass(fs, 1000.0, 0.70710678);
    CHECK(lp.magnitudeDb(1000.0, fs) == doctest::Approx(-3.01).epsilon(0.01));
    CHECK(lp.magnitudeDb(100.0, fs) == doctest::Approx(0.0).scale(1).epsilon(0.02));
    CHECK(lp.magnitudeDb(10000.0, fs) < -38.0);
    CHECK(measureDb(lp, 1000.0) == doctest::Approx(lp.magnitudeDb(1000.0, fs)).epsilon(0.02));
    CHECK(measureDb(lp, 4000.0) == doctest::Approx(lp.magnitudeDb(4000.0, fs)).epsilon(0.02));

    const auto hp = design::highPass(fs, 80.0, 0.70710678);
    CHECK(hp.magnitudeDb(80.0, fs) == doctest::Approx(-3.01).epsilon(0.01));
    CHECK(hp.magnitudeDb(40.0, fs) == doctest::Approx(-12.3).epsilon(0.03));
    CHECK(hp.magnitudeDb(1000.0, fs) == doctest::Approx(0.0).scale(1).epsilon(0.02));

    const auto bell = design::peaking(fs, 1000.0, 6.0, 1.0);
    CHECK(bell.magnitudeDb(1000.0, fs) == doctest::Approx(6.0).epsilon(0.01));
    CHECK(std::fabs(bell.magnitudeDb(100.0, fs)) < 0.1);
    CHECK(std::fabs(bell.magnitudeDb(10000.0, fs)) < 0.1);
    CHECK(measureDb(bell, 1000.0) == doctest::Approx(6.0).epsilon(0.02));

    const auto ls = design::lowShelf(fs, 200.0, -6.0, 0.7);
    CHECK(ls.magnitudeDb(20.0, fs) == doctest::Approx(-6.0).epsilon(0.03));
    CHECK(std::fabs(ls.magnitudeDb(5000.0, fs)) < 0.1);
    const auto hs = design::highShelf(fs, 8000.0, 4.0, 0.7);
    CHECK(hs.magnitudeDb(20000.0, fs) == doctest::Approx(4.0).epsilon(0.05));
    CHECK(std::fabs(hs.magnitudeDb(200.0, fs)) < 0.1);
}

TEST_CASE("biquads stay stable at parameter extremes and clamp above Nyquist") {
    const BiquadCoeffs extremes[] = {design::peaking(fs, 20.0, 18.0, 12.0),      design::peaking(fs, 20000.0, -18.0, 0.2),
                                     design::highPass(44100.0, 25000.0, 12.0),  design::lowPass(44100.0, 20.0, 0.2),
                                     design::lowShelf(96000.0, 20.0, 18.0, 12.0), design::highShelf(44100.0, 20000.0, 18.0, 12.0)};
    for (const auto& c : extremes) {
        Biquad f;
        f.prepare(fs);
        f.setCoefficients(c, true);
        core::AudioBuffer b(1, 96000);
        b.channel(0)[0] = 1.0f;
        f.process(b.channel(0).data(), b.numFrames());
        REQUIRE(test::allFinite(b));
        CHECK(test::peakDb(b, 0, 0, 96000) < 40.0);
        CHECK(test::peakDb(b, 0, 90000, 96000) < -60.0); // impulse response decayed (high-Q 20 Hz rings longest)
    }
}

TEST_CASE("coefficient ramp avoids clicks when switching filters mid-stream") {
    auto measure = [](bool immediate) {
        Biquad f;
        f.prepare(fs);
        f.setCoefficients(design::identity(), true);
        auto b = test::sineRms(1, 9600, fs, 60.0, -6.0);
        f.process(b.channel(0).data(), 4800);
        f.setCoefficients(design::peaking(fs, 60.0, 15.0, 8.0), immediate);
        f.process(b.channel(0).data() + 4800, 4800);
        return test::maxStep(b, 0, 4700, 5100);
    };
    const double ramped = measure(false);
    const double instant = measure(true);
    CHECK(ramped < instant);
    CHECK(ramped < 0.06); // a 60 Hz sine at -6 dBFS has a natural step of ~0.0055
}

TEST_CASE("dc blocker removes offset and passes speech-band audio") {
    DcBlocker dc;
    dc.prepare(fs, 5.0);
    core::AudioBuffer b(1, 48000);
    for (auto& v : b.channel(0))
        v = 0.5f;
    dc.process(b.channel(0).data(), b.numFrames());
    CHECK(std::fabs(b.channel(0)[47999]) < 1e-3f);

    DcBlocker dc2;
    dc2.prepare(fs, 5.0);
    auto s = test::sineRms(1, 48000, fs, 100.0, -20.0);
    dc2.process(s.channel(0).data(), s.numFrames());
    CHECK(test::rmsDb(s, 0, 24000, 48000) == doctest::Approx(-20.0).epsilon(0.002));
}

TEST_CASE("delay line delays by exactly N samples") {
    DelayLine d;
    d.prepare(1000);
    d.setDelay(240);
    std::vector<float> out;
    for (int i = 0; i < 1000; ++i)
        out.push_back(d.processSample(i == 10 ? 1.0f : 0.0f));
    for (int i = 0; i < 1000; ++i)
        CHECK(out[static_cast<std::size_t>(i)] == (i == 250 ? 1.0f : 0.0f));
    d.setDelay(5000); // clamped to prepared max
    CHECK(d.delay() == 1000);
}

TEST_CASE("level detectors read calibrated levels") {
    LevelDetector rms;
    rms.prepare(fs);
    rms.setMode(LevelDetector::Mode::Rms);
    rms.setWindow(10.0);
    auto s = test::sinePeak(1, 48000, fs, 1000.0, 0.0);
    double level = -300.0;
    for (float v : s.channel(0))
        level = rms.processSample(static_cast<double>(v));
    CHECK(level == doctest::Approx(-3.01).epsilon(0.05));

    LevelDetector pk;
    pk.prepare(fs);
    pk.setMode(LevelDetector::Mode::Peak);
    pk.setWindow(10.0);
    double maxL = -300.0, lastL = -300.0;
    for (float v : s.channel(0)) {
        lastL = pk.processSample(static_cast<double>(v));
        maxL = std::max(maxL, lastL);
    }
    CHECK(maxL == doctest::Approx(0.0).scale(1).epsilon(0.01));
    CHECK(lastL > -1.0); // envelope decays only slightly between peaks of a 1 kHz tone
}

TEST_CASE("static gain curves") {
    // Compressor: threshold -20, ratio 4, hard knee.
    CHECK(compressorGainDb(-30.0, -20.0, 4.0, 0.0) == doctest::Approx(0.0));
    CHECK(compressorGainDb(-8.0, -20.0, 4.0, 0.0) == doctest::Approx(-9.0));
    // Soft knee: at threshold the reduction is (1/R-1)*W/8.
    CHECK(compressorGainDb(-20.0, -20.0, 4.0, 6.0) == doctest::Approx(-0.5625));
    CHECK(compressorGainDb(-23.0, -20.0, 4.0, 6.0) == doctest::Approx(0.0));
    CHECK(compressorGainDb(-17.0, -20.0, 4.0, 6.0) == doctest::Approx(-2.25));
    // Knee is continuous with the hard regions.
    CHECK(compressorGainDb(-17.0, -20.0, 4.0, 6.0) == doctest::Approx(compressorGainDb(-16.999, -20.0, 4.0, 6.0)).epsilon(1e-3));

    // Expander: threshold -40, ratio 2, hard knee, cap 10.
    CHECK(expanderGainDb(-30.0, -40.0, 2.0, 0.0, 10.0) == doctest::Approx(0.0));
    CHECK(expanderGainDb(-45.0, -40.0, 2.0, 0.0, 10.0) == doctest::Approx(-5.0));
    CHECK(expanderGainDb(-60.0, -40.0, 2.0, 0.0, 10.0) == doctest::Approx(-10.0)); // capped
    CHECK(expanderGainDb(-120.0, -40.0, 8.0, 0.0, 60.0) == doctest::Approx(-60.0)); // capped
    // Soft knee: at threshold, gain = -(R-1)*W/8.
    CHECK(expanderGainDb(-40.0, -40.0, 4.0, 12.0, 60.0) == doctest::Approx(-4.5));
    CHECK(expanderGainDb(-34.0, -40.0, 4.0, 12.0, 60.0) == doctest::Approx(0.0));
    CHECK(expanderGainDb(-46.0, -40.0, 4.0, 12.0, 60.0) == doctest::Approx(-18.0));
    CHECK(expanderGainDb(-46.0, -40.0, 4.0, 12.0, 60.0) == doctest::Approx(expanderGainDb(-46.001, -40.0, 4.0, 12.0, 60.0)).epsilon(1e-3));
    // Ratio 1 never attenuates.
    CHECK(expanderGainDb(-90.0, -40.0, 1.0, 6.0, 60.0) == doctest::Approx(0.0));
}

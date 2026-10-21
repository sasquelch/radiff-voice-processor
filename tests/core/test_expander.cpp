#include <doctest.h>

#include "TestSignals.h"
#include "radiff/core/modules/Expander.h"

using namespace radiff;
using core::ParamId;
using core::ParameterSet;

namespace {
constexpr double fs = 48000.0;

/// Fast, hard-knee, hold-free baseline so static measurements settle quickly.
ParameterSet baseline() {
    ParameterSet p;
    p.set(ParamId::ExpThresholdDbfs, -30.0f);
    p.set(ParamId::ExpRatio, 2.0f);
    p.set(ParamId::ExpKneeDb, 0.0f);
    p.set(ParamId::ExpHysteresisDb, 0.0f);
    p.set(ParamId::ExpHoldMs, 0.0f);
    p.set(ParamId::ExpAttackMs, 1.0f);
    p.set(ParamId::ExpReleaseMs, 10.0f);
    p.set(ParamId::ExpRmsWindowMs, 2.0f);
    p.set(ParamId::ExpMaxAttenuationDb, 60.0f);
    return p;
}

/// Output level (dBFS RMS) of the last half second of a one-second steady tone.
double steady(const ParameterSet& p, double inRmsDb, std::size_t channels = 1) {
    core::modules::Expander e;
    e.prepare(fs, channels);
    e.update(p);
    auto b = test::sineRms(channels, 48000, fs, 1000.0, inRmsDb);
    test::processInBlocks(b, 512, [&](core::AudioView v) { e.process(v); });
    return test::rmsDb(b, 0, 24000, 48000);
}
} // namespace

TEST_CASE("expander static curve: ratio below threshold, unity above, hard cap") {
    auto p = baseline();
    CHECK(steady(p, -20.0) == doctest::Approx(-20.0).epsilon(0.005));  // above threshold: untouched
    CHECK(steady(p, -40.0) == doctest::Approx(-50.0).epsilon(0.01));   // 10 dB under -> 20 dB under (2:1)
    p.set(ParamId::ExpMaxAttenuationDb, 10.0f);
    CHECK(steady(p, -40.0) == doctest::Approx(-50.0).epsilon(0.01));
    CHECK(steady(p, -60.0) == doctest::Approx(-70.0).epsilon(0.01));   // would be -90: capped at 10 dB
    CHECK(steady(p, -75.0) == doctest::Approx(-85.0).epsilon(0.01));
    p.set(ParamId::ExpRatio, 8.0f);
    p.set(ParamId::ExpMaxAttenuationDb, 0.0f);
    CHECK(steady(p, -60.0) == doctest::Approx(-60.0).epsilon(0.005));  // cap 0 => never attenuates
}

TEST_CASE("soft knee reduces gain gently around the threshold") {
    auto p = baseline();
    p.set(ParamId::ExpRatio, 4.0f);
    p.set(ParamId::ExpKneeDb, 12.0f);
    CHECK(steady(p, -30.0) == doctest::Approx(-34.5).epsilon(0.01));   // (R-1)*W/8 = 4.5 dB at threshold
    CHECK(steady(p, -24.0) == doctest::Approx(-24.0).epsilon(0.005));  // knee edge: untouched
    CHECK(steady(p, -36.0) == doctest::Approx(-54.0).epsilon(0.01));   // below knee: full 4:1
}

TEST_CASE("hold keeps the expander open after speech stops") {
    auto p = baseline();
    p.set(ParamId::ExpHoldMs, 200.0f);
    p.set(ParamId::ExpMaxAttenuationDb, 10.0f);
    core::modules::Expander e;
    e.prepare(fs, 1);
    e.update(p);
    core::AudioBuffer b(1, 48000);
    auto loud = test::sineRms(1, 24000, fs, 1000.0, -20.0);
    auto quiet = test::sineRms(1, 24000, fs, 1000.0, -50.0);
    for (std::size_t i = 0; i < 24000; ++i) {
        b.channel(0)[i] = loud.channel(0)[i];
        b.channel(0)[24000 + i] = quiet.channel(0)[i];
    }
    test::processInBlocks(b, 480, [&](core::AudioView v) { e.process(v); });
    // 100 ms into the quiet part: still held open (gain ~0 dB).
    CHECK(test::rmsDb(b, 0, 24000 + 2400, 24000 + 7200) == doctest::Approx(-50.0).epsilon(0.01));
    // 450 ms in: hold expired, release (10 ms) done -> capped 10 dB attenuation.
    CHECK(test::rmsDb(b, 0, 24000 + 21600, 48000) == doctest::Approx(-60.0).epsilon(0.01));
}

TEST_CASE("hysteresis: a signal between close and open thresholds keeps its state") {
    auto p = baseline();
    p.set(ParamId::ExpHysteresisDb, 6.0f); // opens at -30, closes below -36
    auto run = [&](double firstDb) {
        core::modules::Expander e;
        e.prepare(fs, 1);
        e.update(p);
        core::AudioBuffer b(1, 48000);
        auto first = test::sineRms(1, 24000, fs, 1000.0, firstDb);
        auto mid = test::sineRms(1, 24000, fs, 1000.0, -33.0);
        for (std::size_t i = 0; i < 24000; ++i) {
            b.channel(0)[i] = first.channel(0)[i];
            b.channel(0)[24000 + i] = mid.channel(0)[i];
        }
        test::processInBlocks(b, 512, [&](core::AudioView v) { e.process(v); });
        return test::rmsDb(b, 0, 36000, 48000);
    };
    CHECK(run(-20.0) == doctest::Approx(-33.0).epsilon(0.01)); // was open -> stays open
    CHECK(run(-60.0) == doctest::Approx(-36.0).epsilon(0.01)); // was closed -> stays closed: 3 dB under x 2:1
}

TEST_CASE("release time constant governs closing speed") {
    auto p = baseline();
    p.set(ParamId::ExpReleaseMs, 100.0f);
    p.set(ParamId::ExpMaxAttenuationDb, 10.0f);
    core::modules::Expander e;
    e.prepare(fs, 1);
    e.update(p);
    core::AudioBuffer b(1, 48000);
    auto loud = test::sineRms(1, 24000, fs, 1000.0, -20.0);
    auto quiet = test::sineRms(1, 24000, fs, 1000.0, -60.0);
    for (std::size_t i = 0; i < 24000; ++i) {
        b.channel(0)[i] = loud.channel(0)[i];
        b.channel(0)[24000 + i] = quiet.channel(0)[i];
    }
    test::processInBlocks(b, 480, [&](core::AudioView v) { e.process(v); });
    // Gain target is -10 dB; after one time constant (100 ms) it should be ~63% there.
    const double atTau = test::rmsDb(b, 0, 24000 + 4800 - 240, 24000 + 4800 + 240) + 60.0;
    CHECK(atTau == doctest::Approx(-6.3).epsilon(0.15));
    CHECK(test::rmsDb(b, 0, 46000, 48000) == doctest::Approx(-70.0).epsilon(0.01));
}

TEST_CASE("attack opens quickly when speech returns") {
    auto p = baseline();
    p.set(ParamId::ExpAttackMs, 5.0f);
    p.set(ParamId::ExpMaxAttenuationDb, 10.0f);
    core::modules::Expander e;
    e.prepare(fs, 1);
    e.update(p);
    core::AudioBuffer b(1, 48000);
    auto quiet = test::sineRms(1, 24000, fs, 1000.0, -60.0);
    auto loud = test::sineRms(1, 24000, fs, 1000.0, -20.0);
    for (std::size_t i = 0; i < 24000; ++i) {
        b.channel(0)[i] = quiet.channel(0)[i];
        b.channel(0)[24000 + i] = loud.channel(0)[i];
    }
    test::processInBlocks(b, 64, [&](core::AudioView v) { e.process(v); });
    // Within 25 ms (5 time constants) the gain is back to unity.
    CHECK(test::rmsDb(b, 0, 24000 + 1200, 24000 + 4800) == doctest::Approx(-20.0).epsilon(0.01));
    // No word-onset clipping: the first 2 ms are already above the capped level.
    CHECK(test::rmsDb(b, 0, 24000, 24000 + 96) > -30.0);
}

TEST_CASE("lookahead delays the audio and reports latency") {
    auto p = baseline();
    p.set(ParamId::ExpLookaheadMs, 5.0f);
    p.set(ParamId::ExpEnabled, false);
    core::modules::Expander e;
    e.prepare(fs, 1);
    e.update(p);
    CHECK(e.latencySamples() == 240);
    core::AudioBuffer b(1, 1000);
    b.channel(0)[100] = 1.0f;
    e.process(b.view());
    for (std::size_t i = 0; i < 1000; ++i)
        CHECK(b.channel(0)[i] == (i == 340 ? 1.0f : 0.0f));

    // With the expander active, lookahead lets the gain open BEFORE the onset arrives.
    p.set(ParamId::ExpEnabled, true);
    p.set(ParamId::ExpMaxAttenuationDb, 20.0f);
    p.set(ParamId::ExpAttackMs, 3.0f);
    auto onset = [&](float lookaheadMs) {
        p.set(ParamId::ExpLookaheadMs, lookaheadMs);
        core::modules::Expander x;
        x.prepare(fs, 1);
        x.update(p);
        core::AudioBuffer s(1, 48000);
        auto quiet = test::sineRms(1, 24000, fs, 1000.0, -70.0);
        auto loud = test::sineRms(1, 24000, fs, 1000.0, -20.0);
        for (std::size_t i = 0; i < 24000; ++i) {
            s.channel(0)[i] = quiet.channel(0)[i];
            s.channel(0)[24000 + i] = loud.channel(0)[i];
        }
        test::processInBlocks(s, 128, [&](core::AudioView v) { x.process(v); });
        const std::size_t start = 24000 + x.latencySamples();
        return test::rmsDb(s, 0, start, start + 48); // first millisecond of the onset
    };
    CHECK(onset(5.0f) > onset(0.0f) + 6.0);
}

TEST_CASE("stereo link applies one gain to both channels") {
    auto p = baseline();
    p.set(ParamId::ExpMaxAttenuationDb, 10.0f);
    auto run = [&](bool link) {
        p.set(ParamId::ExpStereoLink, link);
        core::modules::Expander e;
        e.prepare(fs, 2);
        e.update(p);
        core::AudioBuffer b(2, 48000);
        auto loud = test::sineRms(1, 48000, fs, 1000.0, -20.0);
        auto quiet = test::sineRms(1, 48000, fs, 1000.0, -60.0);
        for (std::size_t i = 0; i < 48000; ++i) {
            b.channel(0)[i] = loud.channel(0)[i];
            b.channel(1)[i] = quiet.channel(0)[i];
        }
        test::processInBlocks(b, 512, [&](core::AudioView v) { e.process(v); });
        return test::rmsDb(b, 1, 24000, 48000);
    };
    CHECK(run(true) == doctest::Approx(-60.0).epsilon(0.01));  // loud left keeps quiet right open
    CHECK(run(false) == doctest::Approx(-70.0).epsilon(0.01)); // independent: right is expanded (capped)
}

TEST_CASE("wet/dry mix and disabled state") {
    auto p = baseline();
    p.set(ParamId::ExpMaxAttenuationDb, 10.0f);
    p.set(ParamId::ExpWet, 0.5f);
    // 0.5*dry + 0.5*dry*10^(-10/20) = 0.658 -> -3.63 dB
    CHECK(steady(p, -60.0) == doctest::Approx(-63.63).epsilon(0.01));
    p.set(ParamId::ExpWet, 1.0f);
    p.set(ParamId::ExpEnabled, false);
    CHECK(steady(p, -60.0) == doctest::Approx(-60.0).epsilon(0.005));
}

TEST_CASE("sidechain high-pass stops rumble from holding the expander open") {
    auto p = baseline();
    p.set(ParamId::ExpMaxAttenuationDb, 10.0f);
    auto run = [&](bool scHp) {
        p.set(ParamId::ExpSidechainHighPassEnabled, scHp);
        p.set(ParamId::ExpSidechainHighPassHz, 300.0f);
        core::modules::Expander e;
        e.prepare(fs, 1);
        e.update(p);
        auto b = test::sineRms(1, 48000, fs, 50.0, -20.0); // 50 Hz rumble, above threshold
        test::processInBlocks(b, 512, [&](core::AudioView v) { e.process(v); });
        return test::rmsDb(b, 0, 24000, 48000);
    };
    CHECK(run(false) == doctest::Approx(-20.0).epsilon(0.005));
    CHECK(run(true) == doctest::Approx(-30.0).epsilon(0.01)); // detector sees ~-51 dB -> closed, capped 10 dB
}

TEST_CASE("peak detector mode behaves like RMS on steady tones (within calibration)") {
    auto p = baseline();
    p.set(ParamId::ExpDetector, static_cast<float>(core::DetectorType::Peak));
    p.set(ParamId::ExpMaxAttenuationDb, 10.0f);
    // Peak level of a -40 dBFS RMS sine is -37 dBFS: 7 dB under -> 7 dB more under.
    CHECK(steady(p, -40.0) == doctest::Approx(-47.0).epsilon(0.02));
}

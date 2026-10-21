#include <doctest.h>

#include "TestSignals.h"
#include "radiff/core/modules/Compressor.h"

using namespace radiff;
using core::Module;
using core::ParamId;
using core::ParameterSet;

namespace {
constexpr double fs = 48000.0;

ParameterSet baseline() {
    ParameterSet p;
    p.set(ParamId::Comp1ThresholdDbfs, -20.0f);
    p.set(ParamId::Comp1Ratio, 4.0f);
    p.set(ParamId::Comp1KneeDb, 0.0f);
    p.set(ParamId::Comp1AttackMs, 0.5f);
    p.set(ParamId::Comp1ReleaseMs, 10.0f);
    p.set(ParamId::Comp1RmsWindowMs, 2.0f);
    return p;
}

double steady(const ParameterSet& p, double inRmsDb, std::size_t channels = 1, Module which = Module::Compressor1) {
    core::modules::Compressor c(which);
    c.prepare(fs, channels);
    c.update(p);
    auto b = test::sineRms(channels, 48000, fs, 1000.0, inRmsDb);
    test::processInBlocks(b, 512, [&](core::AudioView v) { c.process(v); });
    return test::rmsDb(b, 0, 24000, 48000);
}

/// Loud/quiet/loud program for timing tests; returns the processed buffer.
core::AudioBuffer stepProgram(core::modules::Compressor& c, double loudDb, double quietDb) {
    core::AudioBuffer b(1, 72000);
    auto loud = test::sineRms(1, 24000, fs, 1000.0, loudDb);
    auto quiet = test::sineRms(1, 24000, fs, 1000.0, quietDb);
    for (std::size_t i = 0; i < 24000; ++i) {
        b.channel(0)[i] = quiet.channel(0)[i];
        b.channel(0)[24000 + i] = loud.channel(0)[i];
        b.channel(0)[48000 + i] = quiet.channel(0)[i];
    }
    test::processInBlocks(b, 240, [&](core::AudioView v) { c.process(v); });
    return b;
}
} // namespace

TEST_CASE("compressor static curve, hard knee") {
    auto p = baseline();
    CHECK(steady(p, -30.0) == doctest::Approx(-30.0).epsilon(0.005)); // below threshold
    CHECK(steady(p, -8.0) == doctest::Approx(-17.0).epsilon(0.01));   // 12 over -> 3 over (4:1)
    p.set(ParamId::Comp1Ratio, 20.0f);
    CHECK(steady(p, -8.0) == doctest::Approx(-19.4).epsilon(0.01));
    p.set(ParamId::Comp1Ratio, 1.0f);
    CHECK(steady(p, -8.0) == doctest::Approx(-8.0).epsilon(0.005));   // 1:1 is transparent
}

TEST_CASE("soft knee and makeup gain") {
    auto p = baseline();
    p.set(ParamId::Comp1KneeDb, 6.0f);
    CHECK(steady(p, -20.0) == doctest::Approx(-20.5625).epsilon(0.01)); // (1/R-1)*W/8
    CHECK(steady(p, -23.0) == doctest::Approx(-23.0).epsilon(0.005));
    CHECK(steady(p, -17.0) == doctest::Approx(-19.25).epsilon(0.01));
    p.set(ParamId::Comp1KneeDb, 0.0f);
    p.set(ParamId::Comp1MakeupGainDb, 6.0f);
    CHECK(steady(p, -8.0) == doctest::Approx(-11.0).epsilon(0.01));
    CHECK(steady(p, -40.0) == doctest::Approx(-34.0).epsilon(0.01)); // makeup applies below threshold too
}

TEST_CASE("attack and release time constants") {
    auto p = baseline();
    p.set(ParamId::Comp1AttackMs, 10.0f);
    p.set(ParamId::Comp1ReleaseMs, 100.0f);
    core::modules::Compressor c;
    c.prepare(fs, 1);
    c.update(p);
    auto b = stepProgram(c, -8.0, -40.0);
    // Target reduction 9 dB. At t = attack (10 ms) ~63% applied -> output ~ -8 - 5.7 = -13.7.
    const double atAttack = test::rmsDb(b, 0, 24000 + 480 - 96, 24000 + 480 + 96);
    CHECK(atAttack == doctest::Approx(-13.7).epsilon(0.06));
    CHECK(test::rmsDb(b, 0, 40000, 48000) == doctest::Approx(-17.0).epsilon(0.01));
    // Release: at t = 100 ms after the drop, 37% of the 9 dB remains -> ~ -40 - 3.3.
    const double atRelease = test::rmsDb(b, 0, 48000 + 4800 - 240, 48000 + 4800 + 240);
    CHECK(atRelease == doctest::Approx(-43.3).epsilon(0.03));
    CHECK(test::rmsDb(b, 0, 68000, 72000) == doctest::Approx(-40.0).epsilon(0.01));
}

TEST_CASE("program-dependent release is slower after sustained reduction") {
    auto p = baseline();
    p.set(ParamId::Comp1ReleaseMs, 50.0f);
    p.set(ParamId::Comp1AutoRelease, true);
    auto recoveryDb = [&](std::size_t loudFrames) {
        core::modules::Compressor c;
        c.prepare(fs, 1);
        c.update(p);
        core::AudioBuffer b(1, 96000 + loudFrames);
        auto quiet = test::sineRms(1, 48000, fs, 1000.0, -40.0);
        auto loud = test::sineRms(1, loudFrames, fs, 1000.0, -2.0);
        for (std::size_t i = 0; i < 48000; ++i)
            b.channel(0)[i] = quiet.channel(0)[i];
        for (std::size_t i = 0; i < loudFrames; ++i)
            b.channel(0)[48000 + i] = loud.channel(0)[i];
        for (std::size_t i = 0; i < 48000; ++i)
            b.channel(0)[48000 + loudFrames + i] = quiet.channel(0)[i];
        test::processInBlocks(b, 256, [&](core::AudioView v) { c.process(v); });
        const std::size_t t = 48000 + loudFrames + 2400; // 50 ms after the drop
        return test::rmsDb(b, 0, t - 120, t + 120);       // closer to -40 = faster recovery
    };
    const double afterBurst = recoveryDb(960);      // 20 ms burst
    const double afterSustained = recoveryDb(96000); // 2 s loud
    CHECK(afterBurst > afterSustained + 1.0);
    // Without auto release both recover identically.
    p.set(ParamId::Comp1AutoRelease, false);
    CHECK(recoveryDb(960) == doctest::Approx(recoveryDb(96000)).epsilon(0.01));
}

TEST_CASE("stereo link uses the louder channel for both") {
    auto p = baseline();
    auto run = [&](bool link) {
        p.set(ParamId::Comp1StereoLink, link);
        core::modules::Compressor c;
        c.prepare(fs, 2);
        c.update(p);
        core::AudioBuffer b(2, 48000);
        auto loud = test::sineRms(1, 48000, fs, 1000.0, -8.0);
        auto quiet = test::sineRms(1, 48000, fs, 1000.0, -40.0);
        for (std::size_t i = 0; i < 48000; ++i) {
            b.channel(0)[i] = loud.channel(0)[i];
            b.channel(1)[i] = quiet.channel(0)[i];
        }
        test::processInBlocks(b, 512, [&](core::AudioView v) { c.process(v); });
        return test::rmsDb(b, 1, 24000, 48000);
    };
    CHECK(run(true) == doctest::Approx(-49.0).epsilon(0.01));  // 9 dB reduction follows the left channel
    CHECK(run(false) == doctest::Approx(-40.0).epsilon(0.005)); // independent: quiet right untouched
}

TEST_CASE("sidechain high-pass, wet mix, peak detector and compressor 2 registry") {
    auto p = baseline();
    p.set(ParamId::Comp1SidechainHighPassEnabled, true);
    p.set(ParamId::Comp1SidechainHighPassHz, 500.0f);
    // 60 Hz at -8 dBFS: detector sees ~-45 dB -> no compression of the bass.
    {
        core::modules::Compressor c;
        c.prepare(fs, 1);
        c.update(p);
        auto b = test::sineRms(1, 48000, fs, 60.0, -8.0);
        test::processInBlocks(b, 512, [&](core::AudioView v) { c.process(v); });
        CHECK(test::rmsDb(b, 0, 24000, 48000) == doctest::Approx(-8.0).epsilon(0.005));
    }
    p = baseline();
    p.set(ParamId::Comp1Wet, 0.5f);
    // 0.5*dry + 0.5*dry*10^(-9/20) = 0.677 -> -3.39 dB
    CHECK(steady(p, -8.0) == doctest::Approx(-11.39).epsilon(0.01));

    p = baseline();
    p.set(ParamId::Comp1Detector, static_cast<float>(core::DetectorType::Peak));
    // Peak of a -8 dBFS RMS sine is -5 dBFS: 15 over -> 11.25 dB reduction.
    CHECK(steady(p, -8.0) == doctest::Approx(-19.25).epsilon(0.02));

    // Compressor 2 reads its own parameters.
    ParameterSet q;
    q.set(ParamId::Comp2Enabled, true);
    q.set(ParamId::Comp2ThresholdDbfs, -20.0f);
    q.set(ParamId::Comp2Ratio, 4.0f);
    q.set(ParamId::Comp2KneeDb, 0.0f);
    q.set(ParamId::Comp2AttackMs, 0.5f);
    q.set(ParamId::Comp2ReleaseMs, 10.0f);
    q.set(ParamId::Comp2Detector, static_cast<float>(core::DetectorType::Rms));
    q.set(ParamId::Comp2RmsWindowMs, 2.0f);
    CHECK(steady(q, -8.0, 1, Module::Compressor2) == doctest::Approx(-17.0).epsilon(0.01));
    q.set(ParamId::Comp2Enabled, false);
    CHECK(steady(q, -8.0, 1, Module::Compressor2) == doctest::Approx(-8.0).epsilon(0.005));
}

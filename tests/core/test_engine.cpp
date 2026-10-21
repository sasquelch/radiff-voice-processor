#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <ostream> // MSVC: operator<<(string_view) needs the full ostream before doctest stringifies values

#include "TestSignals.h"
#include "radiff/core/Engine.h"

#include <cmath>

using namespace radiff;
using core::Engine;
using core::EngineConfig;
using core::ParamId;
using core::ParameterSet;

namespace {
constexpr double fs = 48000.0;

ParameterSet allOff() {
    ParameterSet p;
    p.set(ParamId::InputDcBlockEnabled, false);
    p.set(ParamId::InputHighPassEnabled, false);
    p.set(ParamId::EqEnabled, false);
    p.set(ParamId::ExpEnabled, false);
    p.set(ParamId::Comp1Enabled, false);
    p.set(ParamId::Comp2Enabled, false);
    return p;
}
} // namespace

TEST_CASE("all modules off: bit-transparent, sample-aligned pass-through") {
    Engine e;
    e.prepare({fs, 256, 2});
    e.setParameters(allOff());
    CHECK(e.latencyFrames() == 0);
    auto buf = test::sineRms(2, 4096, fs, 440.0, -12.0);
    auto ref = buf;
    test::processInBlocks(buf, 256, [&](core::AudioView v) { e.process(v); });
    CHECK(test::maxAbsDiff(buf, ref) == 0.0);
}

TEST_CASE("full bypass returns the source exactly, including when lookahead latency exists") {
    ParameterSet p; // balanced defaults (processing active)
    p.set(ParamId::Bypass, true);
    Engine e;
    e.prepare({fs, 512, 1});
    e.setParameters(p);
    auto buf = test::sineRms(1, 9600, fs, 300.0, -10.0);
    auto ref = buf;
    test::processInBlocks(buf, 512, [&](core::AudioView v) { e.process(v); });
    CHECK(test::maxAbsDiff(buf, ref) == 0.0);

    p.set(ParamId::ExpLookaheadMs, 10.0f);
    Engine e2;
    e2.prepare({fs, 512, 1});
    e2.setParameters(p);
    CHECK(e2.latencyFrames() == 480);
    core::AudioBuffer imp(1, 2000);
    imp.channel(0)[100] = 1.0f;
    test::processInBlocks(imp, 512, [&](core::AudioView v) { e2.process(v); });
    for (std::size_t i = 0; i < 2000; ++i)
        CHECK(imp.channel(0)[i] == (i == 580 ? 1.0f : 0.0f));
}

TEST_CASE("input and output gain are exact in dB through the whole chain") {
    auto p = allOff();
    p.set(ParamId::InputGainDb, 3.0f);
    p.set(ParamId::OutputGainDb, -9.0f);
    Engine e;
    e.prepare({fs, 512, 1});
    e.setParameters(p);
    auto buf = test::sineRms(1, 48000, fs, 1000.0, -20.0);
    test::processInBlocks(buf, 512, [&](core::AudioView v) { e.process(v); });
    CHECK(test::rmsDb(buf, 0, 24000, 48000) == doctest::Approx(-26.0).epsilon(0.002));
}

TEST_CASE("proof of function: balanced defaults change the sound and reduce dynamics") {
    Engine e;
    e.prepare({fs, 512, 1});
    e.setParameters(ParameterSet{});
    // Loud -10 dBFS speech-like tone followed by two seconds of a -60 dBFS noise-floor tone.
    core::AudioBuffer buf(1, 144000);
    auto loud = test::sineRms(1, 48000, fs, 1000.0, -10.0);
    auto quiet = test::sineRms(1, 96000, fs, 1000.0, -60.0);
    for (std::size_t i = 0; i < 48000; ++i)
        buf.channel(0)[i] = loud.channel(0)[i];
    for (std::size_t i = 0; i < 96000; ++i)
        buf.channel(0)[48000 + i] = quiet.channel(0)[i];
    auto ref = buf;
    test::processInBlocks(buf, 512, [&](core::AudioView v) { e.process(v); });
    REQUIRE(test::allFinite(buf));
    CHECK(test::maxAbsDiff(buf, ref) > 0.05);
    // Compressor 1 (-24 dBFS, 2.5:1) pulls the loud part down by (14 * 0.6) = 8.4 dB.
    CHECK(test::rmsDb(buf, 0, 40000, 48000) == doctest::Approx(-18.4).epsilon(0.02));
    // Expander (-48 dBFS, 2:1, cap 10 dB) attenuates the floor by the full 10 dB, never more.
    CHECK(test::rmsDb(buf, 0, 138000, 144000) == doctest::Approx(-70.0).epsilon(0.002));
    const auto m = e.meters();
    CHECK(m.expanderReductionDb == doctest::Approx(10.0).epsilon(0.01));
    CHECK(m.compressor1ReductionDb < 0.5f);
    CHECK(m.outputPeak[0] > 0.0f);
}

TEST_CASE("block size does not change the output") {
    ParameterSet p; // full default chain, including dynamics state
    p.set(ParamId::ExpLookaheadMs, 2.0f);
    auto run = [&](std::size_t block) {
        Engine e;
        e.prepare({fs, 4096, 2});
        e.setParameters(p);
        core::AudioBuffer buf(2, 48000);
        auto a = test::sineRms(1, 48000, fs, 700.0, -10.0);
        for (std::size_t i = 0; i < 48000; ++i) {
            const float env = (i / 8000) % 2 == 0 ? 1.0f : 0.01f; // alternating loud/quiet
            buf.channel(0)[i] = a.channel(0)[i] * env;
            buf.channel(1)[i] = a.channel(0)[i] * env * 0.5f;
        }
        test::processInBlocks(buf, block, [&](core::AudioView v) { e.process(v); });
        return buf;
    };
    const auto a = run(1);
    const auto b = run(64);
    const auto c = run(4096);
    const auto d = run(1000); // not a divisor of the length
    CHECK(test::maxAbsDiff(a, b) < 1e-6);
    CHECK(test::maxAbsDiff(a, c) < 1e-6);
    CHECK(test::maxAbsDiff(a, d) < 1e-6);
}

TEST_CASE("behaviour is consistent across 44.1, 48 and 96 kHz") {
    ParameterSet p;
    p.set(ParamId::EqBand3Enabled, true);
    p.set(ParamId::EqBand3GainDb, 4.0f);
    double ref = 0.0;
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        Engine e;
        e.prepare({rate, 512, 1});
        e.setParameters(p);
        auto buf = test::sineRms(1, static_cast<std::size_t>(rate), rate, 1200.0, -14.0);
        test::processInBlocks(buf, 512, [&](core::AudioView v) { e.process(v); });
        REQUIRE(test::allFinite(buf));
        const double level = test::rmsDb(buf, 0, static_cast<std::size_t>(rate / 2), static_cast<std::size_t>(rate));
        if (ref == 0.0)
            ref = level;
        CHECK(level == doctest::Approx(ref).epsilon(0.01));
    }
}

TEST_CASE("silence in, silence out; huge input stays finite; parameters clamp") {
    Engine e;
    e.prepare({fs, 512, 2});
    e.setParameters(ParameterSet{});
    core::AudioBuffer z(2, 4096);
    test::processInBlocks(z, 512, [&](core::AudioView v) { e.process(v); });
    for (std::size_t c = 0; c < 2; ++c)
        CHECK(test::peakDb(z, c, 0, 4096) < -200.0);

    ParameterSet p;
    p.set(ParamId::InputGainDb, 24.0f);
    p.set(ParamId::Comp1MakeupGainDb, 24.0f);
    p.set(ParamId::OutputGainDb, 24.0f);
    e.setParameters(p);
    auto loud = test::sineRms(2, 4096, fs, 50.0, 6.0);
    test::processInBlocks(loud, 512, [&](core::AudioView v) { e.process(v); });
    CHECK(test::allFinite(loud));
}

TEST_CASE("parameter changes mid-stream are smooth (no zipper, no clicks)") {
    Engine e;
    e.prepare({fs, 64, 1});
    auto p = allOff();
    e.setParameters(p);
    auto buf = test::sineRms(1, 48000, fs, 200.0, -6.0);
    std::size_t pos = 0;
    test::processInBlocks(buf, 64, [&](core::AudioView v) {
        if (pos == 12800) {
            p.set(ParamId::OutputGainDb, -18.0f);
            p.set(ParamId::EqEnabled, true);
            p.set(ParamId::EqBand2Enabled, true);
            p.set(ParamId::EqBand2FrequencyHz, 2000.0f); // far from the 200 Hz test tone
            p.set(ParamId::EqBand2GainDb, 10.0f);
            p.set(ParamId::InputDcBlockEnabled, true);
            e.setParameters(p);
        }
        e.process(v);
        pos += v.numFrames;
    });
    CHECK(test::maxStep(buf, 0, 12000, 20000) < 0.03); // natural step of this sine is ~0.0185
    CHECK(test::rmsDb(buf, 0, 40000, 48000) == doctest::Approx(-6.0 - 18.0).epsilon(0.01)); // +0.13 dB skirt of the 2 kHz bell
}

TEST_CASE("independent instances do not share state") {
    Engine a, b;
    a.prepare({fs, 256, 1});
    b.prepare({fs, 256, 1});
    a.setParameters(ParameterSet{});
    b.setParameters(ParameterSet{});
    auto loud = test::sineRms(1, 24000, fs, 1000.0, -6.0);
    test::processInBlocks(loud, 256, [&](core::AudioView v) { a.process(v); }); // a now has compression state
    auto x = test::sineRms(1, 256, fs, 1000.0, -30.0);
    auto y = x;
    b.process(y.view());
    Engine fresh;
    fresh.prepare({fs, 256, 1});
    fresh.setParameters(ParameterSet{});
    fresh.process(x.view());
    CHECK(test::maxAbsDiff(x, y) == 0.0);
}

TEST_CASE("engine: speech level drives the relative expander threshold; latency = max(lookahead, reject)") {
    core::Engine e;
    core::EngineConfig cfg;
    cfg.sampleRate = 48000.0;
    cfg.numChannels = 1;
    e.prepare(cfg);
    ParameterSet p;
    p.set(ParamId::ExpThresholdDbfs, -48.0f);
    p.set(ParamId::ExpThresholdMode, static_cast<float>(core::ThresholdMode::SpeechRelative));
    p.set(ParamId::ExpRelativeThresholdDb, -16.0f);
    p.set(ParamId::ExpLookaheadMs, 3.0f);
    p.set(ParamId::ExpTransientRejectMs, 8.0f);
    e.setParameters(p);
    CHECK(e.latencyFrames() == 384);
    CHECK(e.expander().effectiveThresholdDb() == doctest::Approx(-48.0)); // nothing measured yet
    e.setSpeechLevelDb(-22.0);
    CHECK(e.expander().effectiveThresholdDb() == doctest::Approx(-38.0));
    // Survives parameter updates and a re-prepare.
    p.set(ParamId::ExpRatio, 3.0f);
    e.setParameters(p);
    CHECK(e.expander().effectiveThresholdDb() == doctest::Approx(-38.0));
    e.prepare(cfg);
    CHECK(e.expander().effectiveThresholdDb() == doctest::Approx(-38.0));
    CHECK(e.speechLevelDb() == doctest::Approx(-22.0));
    // Bypass stays sample-exact with the combined delay in place.
    p.set(ParamId::Bypass, 1.0f);
    e.setParameters(p);
    core::AudioBuffer in(1, 4800);
    for (std::size_t i = 0; i < in.numFrames(); ++i)
        in.channel(0)[i] = static_cast<float>(std::sin(0.05 * static_cast<double>(i)));
    core::AudioBuffer out(in);
    test::processInBlocks(out, 256, [&](core::AudioView v) { e.process(v); });
    CHECK(test::maxAbsDiff(in, out, 384) == 0.0f);
}

TEST_CASE("online speech tracking drives the speech-relative threshold in realtime hosts") {
    core::Engine e;
    e.prepare({48000.0, 480, 1});
    core::ParameterSet p;
    p.set(ParamId::ExpThresholdMode, static_cast<float>(core::ThresholdMode::SpeechRelative));
    p.set(ParamId::ExpRelativeThresholdDb, -16.0f);
    e.setParameters(p);
    e.setSpeechTracking(true, 10.0);
    CHECK(e.expander().effectiveThresholdDb() == doctest::Approx(-48.0)); // absolute until measured
    CHECK_FALSE(e.trackedSpeechLevelDb().has_value());

    auto feed = [&](double rmsDb, double seconds) {
        auto b = test::sineRms(1, 48000, 48000.0, 1000.0, rmsDb);
        for (int s = 0; s < static_cast<int>(seconds); ++s) {
            auto copy = b;
            test::processInBlocks(copy, 480, [&](core::AudioView v) { e.process(v); });
        }
    };
    feed(-20.0, 3);
    REQUIRE(e.trackedSpeechLevelDb().has_value());
    CHECK(*e.trackedSpeechLevelDb() == doctest::Approx(-20.0).epsilon(0.02));
    CHECK(e.expander().effectiveThresholdDb() == doctest::Approx(-36.0).epsilon(0.01));

    // A parameter change must not clobber the tracked level.
    p.set(ParamId::ExpRatio, 3.0f);
    e.setParameters(p);
    CHECK(e.expander().effectiveThresholdDb() == doctest::Approx(-36.0).epsilon(0.01));

    // The tracker forgets: after 60 s at -30 dBFS (6 time constants) it has followed.
    feed(-30.0, 60);
    CHECK(*e.trackedSpeechLevelDb() == doctest::Approx(-30.0).epsilon(0.04));
    CHECK(e.expander().effectiveThresholdDb() == doctest::Approx(-46.0).epsilon(0.03));

    // Disabling tracking falls back to the host value / absolute threshold.
    e.setSpeechTracking(false);
    CHECK(e.expander().effectiveThresholdDb() == doctest::Approx(-48.0));
    e.setSpeechLevelDb(-25.0);
    CHECK(e.expander().effectiveThresholdDb() == doctest::Approx(-41.0));
}

TEST_CASE("de-esser and enhancer are in the engine chain and meter") {
    core::Engine e;
    e.prepare({48000.0, 512, 1});
    core::ParameterSet p;
    p.set(ParamId::ExpEnabled, false);
    p.set(ParamId::Comp1Enabled, false);
    p.set(ParamId::DeessEnabled, true);
    p.set(ParamId::DeessThresholdDbfs, -60.0f);
    p.set(ParamId::DeessMaxReductionDb, 6.0f);
    p.set(ParamId::PresEnabled, true);
    p.set(ParamId::PresMakeupGainDb, -3.0f);
    e.setParameters(p);
    auto b = test::sineRms(1, 48000, 48000.0, 6000.0, -20.0);
    test::processInBlocks(b, 512, [&](core::AudioView v) { e.process(v); });
    // 6 dB from the de-esser band reduction, 3 dB enhancer makeup.
    CHECK(test::toneLevelDb(b, 0, 24000, 48000, 48000.0, 6000.0) == doctest::Approx(-29.0).epsilon(0.02));
    CHECK(e.meters().deesserReductionDb == doctest::Approx(6.0).epsilon(0.02));
}

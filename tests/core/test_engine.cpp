#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "radiff/core/Engine.h"

#include <cmath>

using namespace radiff::core;

namespace {
AudioBuffer makeSine(std::size_t channels, std::size_t frames, float amp = 0.5f) {
    AudioBuffer b(channels, frames);
    for (std::size_t c = 0; c < channels; ++c)
        for (std::size_t i = 0; i < frames; ++i)
            b.channel(c)[i] = amp * std::sin(0.05f * static_cast<float>(i));
    return b;
}
} // namespace

TEST_CASE("engine passes audio through unchanged at default parameters") {
    Engine e;
    e.prepare({48000.0, 256, 1});
    auto buf = makeSine(1, 256);
    auto ref = buf;
    e.process(buf.view());
    for (std::size_t i = 0; i < 256; ++i)
        CHECK(buf.channel(0)[i] == doctest::Approx(ref.channel(0)[i]));
}

TEST_CASE("engine applies output gain in dB") {
    Engine e;
    e.prepare({44100.0, 512, 2});
    ParameterSet p;
    p.set(ParamId::OutputGainDb, -6.0206f);
    e.setParameters(p);
    auto buf = makeSine(2, 512);
    auto ref = buf;
    e.process(buf.view());
    for (std::size_t c = 0; c < 2; ++c)
        for (std::size_t i = 0; i < 512; ++i)
            CHECK(buf.channel(c)[i] == doctest::Approx(ref.channel(c)[i] * 0.5f).epsilon(1e-4));
}

TEST_CASE("bypass leaves audio untouched") {
    Engine e;
    e.prepare({48000.0, 128, 1});
    ParameterSet p;
    p.set(ParamId::OutputGainDb, 12.0f);
    p.set(ParamId::Bypass, 1.0f);
    e.setParameters(p);
    auto buf = makeSine(1, 128);
    auto ref = buf;
    e.process(buf.view());
    for (std::size_t i = 0; i < 128; ++i)
        CHECK(buf.channel(0)[i] == ref.channel(0)[i]);
}

TEST_CASE("output is independent of block size") {
    constexpr std::size_t frames = 4096;
    ParameterSet p;
    p.set(ParamId::InputGainDb, 3.0f);
    p.set(ParamId::OutputGainDb, -1.5f);

    auto a = makeSine(1, frames);
    auto b = makeSine(1, frames);

    Engine ea;
    ea.prepare({48000.0, 4096, 1});
    ea.setParameters(p);
    ea.process(a.view());

    Engine eb;
    eb.prepare({48000.0, 64, 1});
    eb.setParameters(p);
    for (std::size_t pos = 0; pos < frames; pos += 64)
        eb.process(b.view(pos, 64));

    for (std::size_t i = 0; i < frames; ++i)
        CHECK(a.channel(0)[i] == doctest::Approx(b.channel(0)[i]).epsilon(1e-6));
}

TEST_CASE("engine never produces non-finite output from finite input") {
    Engine e;
    e.prepare({96000.0, 1024, 2});
    ParameterSet p;
    p.set(ParamId::InputGainDb, 24.0f);
    p.set(ParamId::OutputGainDb, 24.0f);
    e.setParameters(p);
    auto buf = makeSine(2, 1024, 1.0f);
    e.process(buf.view());
    for (std::size_t c = 0; c < 2; ++c)
        for (float v : buf.channel(c))
            CHECK(std::isfinite(v));
}

TEST_CASE("silence in, silence out") {
    Engine e;
    e.prepare({48000.0, 512, 1});
    AudioBuffer buf(1, 512);
    e.process(buf.view());
    for (float v : buf.channel(0))
        CHECK(v == 0.0f);
}

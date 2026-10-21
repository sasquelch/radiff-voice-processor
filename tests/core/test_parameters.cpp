#include <doctest.h>

#include "radiff/core/Parameters.h"

#include <set>
#include <string>

using namespace radiff::core;

TEST_CASE("parameter table integrity") {
    std::set<std::string> paths;
    for (const auto& p : paramTable()) {
        const std::string path = paramPath(p.id);
        CHECK(paths.insert(path).second); // unique
        auto id = paramIdForPath(path);
        REQUIRE_MESSAGE(id.has_value(), path);
        CHECK(*id == p.id);
        CHECK(p.minValue <= p.defaultValue);
        CHECK(p.defaultValue <= p.maxValue);
        if (p.isEnum())
            CHECK(p.enumNames.size() == static_cast<std::size_t>(p.maxValue) + 1);
    }
    CHECK(paths.size() == kNumParams);
    CHECK_FALSE(paramIdForPath("expander.nope").has_value());
    CHECK_FALSE(paramIdForPath("eq.band9.gainDb").has_value());
    CHECK_FALSE(paramIdForPath("nomodule.x").has_value());
    CHECK(paramIdForPath("eq.band3.gainDb") == ParamId::EqBand3GainDb);
    CHECK(paramIdForPath("compressor2.ratio") == ParamId::Comp2Ratio);
    CHECK(paramIdForPath("output.gainDb") == ParamId::OutputGainDb);
    CHECK(paramIdForPath("global.bypass") == ParamId::Bypass);
}

TEST_CASE("spec balanced starting values are the defaults") {
    ParameterSet s;
    CHECK(s.get(ParamId::InputGainDb) == 0.0f);
    CHECK(s.getBool(ParamId::InputDcBlockEnabled));
    CHECK(s.getBool(ParamId::InputHighPassEnabled));
    CHECK(s.get(ParamId::InputHighPassHz) == 80.0f);
    CHECK(s.getBool(ParamId::ExpEnabled));
    CHECK(s.get(ParamId::ExpThresholdDbfs) == -48.0f);
    CHECK(s.get(ParamId::ExpRatio) == 2.0f);
    CHECK(s.get(ParamId::ExpKneeDb) == 6.0f);
    CHECK(s.get(ParamId::ExpAttackMs) == 5.0f);
    CHECK(s.get(ParamId::ExpHoldMs) == 120.0f);
    CHECK(s.get(ParamId::ExpReleaseMs) == 250.0f);
    CHECK(s.get(ParamId::ExpHysteresisDb) == 4.0f);
    CHECK(s.get(ParamId::ExpMaxAttenuationDb) == 10.0f);
    CHECK(s.getInt(ParamId::ExpDetector) == static_cast<int>(DetectorType::Rms));
    CHECK(s.get(ParamId::ExpWet) == 1.0f);
    CHECK(s.getBool(ParamId::Comp1Enabled));
    CHECK(s.get(ParamId::Comp1ThresholdDbfs) == -24.0f);
    CHECK(s.get(ParamId::Comp1Ratio) == 2.5f);
    CHECK(s.get(ParamId::Comp1KneeDb) == 6.0f);
    CHECK(s.get(ParamId::Comp1AttackMs) == 10.0f);
    CHECK(s.get(ParamId::Comp1ReleaseMs) == 150.0f);
    CHECK(s.get(ParamId::Comp1MakeupGainDb) == 0.0f);
    CHECK_FALSE(s.getBool(ParamId::Comp2Enabled));
    CHECK(s.get(ParamId::OutputGainDb) == 0.0f);
    for (int b = 0; b < kNumEqBands; ++b)
        CHECK_FALSE(s.getBool(ParameterSet::eqParam(b, "enabled")));
}

TEST_CASE("clamping, stepping and value parsing") {
    ParameterSet s;
    s.set(ParamId::OutputGainDb, 1000.0f);
    CHECK(s.get(ParamId::OutputGainDb) == 24.0f);
    s.set(ParamId::OutputGainDb, -1000.0f);
    CHECK(s.get(ParamId::OutputGainDb) == -24.0f);
    s.set(ParamId::InputHighPassSlopeDbOct, 17.0f);
    CHECK(s.get(ParamId::InputHighPassSlopeDbOct) == 12.0f);
    s.set(ParamId::InputHighPassSlopeDbOct, 19.0f);
    CHECK(s.get(ParamId::InputHighPassSlopeDbOct) == 24.0f);
    s.set(ParamId::Bypass, 0.7f);
    CHECK(s.get(ParamId::Bypass) == 1.0f);
    s.set(ParamId::ExpRatio, std::nanf(""));
    CHECK(s.get(ParamId::ExpRatio) == 2.0f); // NaN -> default

    CHECK(parseParamValue(ParamId::ExpDetector, "peak") == 1.0f);
    CHECK(parseParamValue(ParamId::ExpDetector, "RMS") == 0.0f);
    CHECK_FALSE(parseParamValue(ParamId::ExpDetector, "banana").has_value());
    CHECK(parseParamValue(ParamId::EqBand1Type, "highShelf") == 2.0f);
    CHECK(parseParamValue(ParamId::ExpEnabled, "true") == 1.0f);
    CHECK(parseParamValue(ParamId::ExpEnabled, "off") == 0.0f);
    CHECK(parseParamValue(ParamId::ExpThresholdDbfs, "-42.5") == -42.5f);
    CHECK(parseParamValue(ParamId::ExpThresholdDbfs, "1e1") == 10.0f);
    CHECK_FALSE(parseParamValue(ParamId::ExpThresholdDbfs, "-42,5").has_value());
    CHECK_FALSE(parseParamValue(ParamId::ExpThresholdDbfs, "").has_value());
    CHECK(enumName(ParamId::Comp1Detector, 1.0f) == "peak");
    CHECK(enumName(ParamId::Comp1Ratio, 1.0f).empty());
}

TEST_CASE("parameter sets compare by value") {
    ParameterSet a, b;
    CHECK(a == b);
    b.set(ParamId::EqBand4Q, 2.0f);
    CHECK_FALSE(a == b);
}

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "radiff/platform/Paths.h"
#include "radiff/presets/Preset.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using namespace radiff;
using core::ParamId;

namespace {
std::filesystem::path tmpDir() {
    auto d = platform::pathFromUtf8(RADIFF_TEST_TMP_DIR) / "presets";
    std::filesystem::create_directories(d);
    return d;
}
} // namespace

TEST_CASE("preset json round trip is lossless and canonical") {
    presets::Preset p;
    p.name = "Morning Show";
    p.description = "Bright, tight";
    p.author = "Jöe";
    p.parameters.set(ParamId::InputGainDb, 2.5f);
    p.parameters.set(ParamId::OutputGainDb, -4.0f);
    p.parameters.set(ParamId::EqBand2Enabled, true);
    p.parameters.set(ParamId::EqBand2Type, static_cast<float>(core::EqBandType::HighShelf));
    p.parameters.set(ParamId::EqBand2Q, 0.7f);
    p.parameters.set(ParamId::ExpDetector, static_cast<float>(core::DetectorType::Peak));
    p.parameters.set(ParamId::Comp2Enabled, true);

    const std::string text = presets::toJson(p);
    CHECK(text.find("\"schemaVersion\": 1") != std::string::npos);
    CHECK(text.find("\"detector\": \"peak\"") != std::string::npos);
    CHECK(text.find("\"type\": \"highShelf\"") != std::string::npos);
    CHECK(text.find("\"q\": 0.7") != std::string::npos);     // no float noise
    CHECK(text.find("0.699999") == std::string::npos);
    CHECK(text.find('\r') == std::string::npos);            // LF only
    CHECK(text.find("\"signalOrder\"") != std::string::npos);

    presets::Preset q;
    auto r = presets::fromJson(text, q);
    REQUIRE_MESSAGE(r.ok(), r.error);
    CHECK(r.warnings.empty());
    CHECK(q == p);
    CHECK(presets::toJson(q) == text);
}

TEST_CASE("the spec's example structure loads, with unknown future modules warned and ignored") {
    const std::string text = R"({
      "schemaVersion": 1,
      "engineVersion": "1.0.0",
      "name": "Broadcast Voice - Balanced",
      "signalOrder": ["input", "eq", "expander", "multibandExpander", "deesser", "compressor1", "compressor2", "presence", "saturation", "limiter", "output"],
      "modules": {
        "input": {"gainDb": 0, "dcBlockEnabled": true, "highPassEnabled": true, "highPassHz": 80, "highPassSlopeDbOct": 12},
        "eq": {"enabled": true, "bands": [
          {"enabled": false, "type": "lowShelf", "frequencyHz": 120, "gainDb": 0, "q": 0.7},
          {"enabled": true, "type": "bell", "frequencyHz": 300, "gainDb": -2, "q": 1}
        ]},
        "expander": {"enabled": true, "thresholdDbfs": -48, "ratio": 2, "kneeDb": 6, "attackMs": 5, "holdMs": 120, "releaseMs": 250, "hysteresisDb": 4, "detector": "rms", "rmsWindowMs": 10, "lookaheadMs": 0, "maxAttenuationDb": 10, "wet": 1, "stereoLink": true},
        "multibandExpander": {"enabled": false},
        "deesser": {"enabled": false},
        "compressor1": {"enabled": true, "thresholdDbfs": -24, "ratio": 2.5, "kneeDb": 6, "attackMs": 10, "releaseMs": 150, "makeupGainDb": 0, "detector": "rms", "stereoLink": true},
        "compressor2": {"enabled": false},
        "presence": {"enabled": false},
        "saturation": {"enabled": false},
        "limiter": {"enabled": false}
      },
      "output": {"gainDb": -1.5}
    })";
    presets::Preset p;
    auto r = presets::fromJson(text, p);
    REQUIRE_MESSAGE(r.ok(), r.error);
    CHECK(p.name == "Broadcast Voice - Balanced");
    CHECK(p.parameters.get(ParamId::OutputGainDb) == -1.5f);
    CHECK(p.parameters.getBool(ParamId::EqBand2Enabled));
    CHECK(p.parameters.get(ParamId::EqBand2GainDb) == -2.0f);
    CHECK(p.parameters.get(ParamId::ExpThresholdDbfs) == -48.0f);
    CHECK(p.parameters.get(ParamId::Comp1Ratio) == 2.5f);
    // Missing bands/params keep defaults.
    CHECK(p.parameters.get(ParamId::EqBand5FrequencyHz) == 10000.0f);
    CHECK(p.parameters.get(ParamId::Comp1RmsWindowMs) == 10.0f);
    // 5 unknown modules in signalOrder + 5 unknown module objects = 10 warnings.
    CHECK(r.warnings.size() == 10);
}

TEST_CASE("unknown keys warn, values clamp, strings/bools/numbers interchange") {
    const std::string text = R"({
      "schemaVersion": 1, "name": "x",
      "modules": {"expander": {"thresholdDbfs": 999, "enabled": 1, "detector": 1, "future": 3}},
      "output": {"gainDb": "-3"},
      "machinePath": "C:\\nope"
    })";
    presets::Preset p;
    auto r = presets::fromJson(text, p);
    REQUIRE_MESSAGE(r.ok(), r.error);
    CHECK(p.parameters.get(ParamId::ExpThresholdDbfs) == 0.0f); // clamped to max
    CHECK(p.parameters.getBool(ParamId::ExpEnabled));
    CHECK(p.parameters.getInt(ParamId::ExpDetector) == 1);
    CHECK(p.parameters.get(ParamId::OutputGainDb) == -3.0f);
    REQUIRE(r.warnings.size() == 2);
}

TEST_CASE("rejects wrong schema, bad version, bad signal order and invalid json") {
    presets::Preset p;
    CHECK_FALSE(presets::fromJson("{\"format\":\"radvoice\"}", p).ok());
    CHECK_FALSE(presets::fromJson("{\"schemaVersion\":99}", p).ok());
    CHECK_FALSE(presets::fromJson("{\"schemaVersion\":1,\"signalOrder\":[\"compressor1\",\"input\"]}", p).ok());
    CHECK_FALSE(presets::fromJson("{\"schemaVersion\":1,\"modules\":[]}", p).ok());
    CHECK_FALSE(presets::fromJson("{not json", p).ok());
    CHECK_FALSE(presets::fromJson("[]", p).ok());
    CHECK(presets::fromJson("{\"schemaVersion\":1}", p).ok()); // minimal = all defaults
}

TEST_CASE("file load/save with BOM tolerance and canonical bytes") {
    presets::Preset p;
    p.name = "BOM";
    const auto path = tmpDir() / "bom.radvoice.json";
    REQUIRE(presets::savePresetFile(path, p).ok());
    std::string text = presets::toJson(p);
    { std::ofstream o(path, std::ios::binary); o << "\xEF\xBB\xBF" << text; }
    presets::Preset q;
    auto r = presets::loadPresetFile(path, q);
    REQUIRE_MESSAGE(r.ok(), r.error);
    CHECK(q == p);
    CHECK_FALSE(presets::loadPresetFile(tmpDir() / "missing.radvoice.json", q).ok());
}

TEST_CASE("every factory preset loads cleanly") {
    const auto dir = platform::pathFromUtf8(RADIFF_FACTORY_PRESET_DIR);
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".json")
            continue;
        presets::Preset p;
        auto r = presets::loadPresetFile(entry.path(), p);
        CAPTURE(platform::pathToUtf8(entry.path()));
        REQUIRE_MESSAGE(r.ok(), r.error);
        CHECK(r.warnings.empty());
        CHECK_FALSE(p.name.empty());
        CHECK(presets::toJson(p) == [&] { std::ifstream in(entry.path(), std::ios::binary); return std::string((std::istreambuf_iterator<char>(in)), {}); }());
        ++count;
    }
    CHECK(count >= 10);
}

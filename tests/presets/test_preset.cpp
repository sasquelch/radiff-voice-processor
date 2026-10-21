#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "radiff/platform/Paths.h"
#include "radiff/presets/Preset.h"

#include <filesystem>
#include <fstream>

using namespace radiff;

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
    p.parameters.set(core::ParamId::InputGainDb, 2.5f);
    p.parameters.set(core::ParamId::OutputGainDb, -4.0f);

    const std::string text = presets::toJson(p);
    CHECK(text.find("\"format\": \"radvoice\"") != std::string::npos);
    CHECK(text.find('\\') == std::string::npos); // no escaped paths or odd content
    CHECK(text.find("\r") == std::string::npos);  // LF only, on every platform

    presets::Preset q;
    auto r = presets::fromJson(text, q);
    REQUIRE_MESSAGE(r.ok(), r.error);
    CHECK(r.warnings.empty());
    CHECK(q == p);
    CHECK(presets::toJson(q) == text);
}

TEST_CASE("missing parameters take defaults, unknown keys warn, values clamp") {
    const std::string text = R"({
        "format": "radvoice", "format_version": 1, "name": "x",
        "parameters": { "output_gain_db": 999, "future_param": 1.0, "bypass": true },
        "machine_path": "C:\\should\\not\\be\\here"
    })";
    presets::Preset p;
    auto r = presets::fromJson(text, p);
    REQUIRE_MESSAGE(r.ok(), r.error);
    CHECK(p.parameters.get(core::ParamId::OutputGainDb) == core::paramInfo(core::ParamId::OutputGainDb).maxValue);
    CHECK(p.parameters.get(core::ParamId::InputGainDb) == core::paramInfo(core::ParamId::InputGainDb).defaultValue);
    CHECK(p.parameters.get(core::ParamId::Bypass) == 1.0f);
    REQUIRE(r.warnings.size() == 2);
}

TEST_CASE("rejects wrong format, bad version and invalid json") {
    presets::Preset p;
    CHECK_FALSE(presets::fromJson("{\"format\":\"other\"}", p).ok());
    CHECK_FALSE(presets::fromJson("{\"format\":\"radvoice\",\"format_version\":99}", p).ok());
    CHECK_FALSE(presets::fromJson("{not json", p).ok());
    CHECK_FALSE(presets::fromJson("[]", p).ok());
}

TEST_CASE("file load/save with BOM tolerance") {
    presets::Preset p;
    p.name = "BOM";
    const auto path = tmpDir() / "bom.radvoice.json";
    REQUIRE(presets::savePresetFile(path, p).ok());
    // Re-write with a UTF-8 BOM, as Notepad would.
    std::string text = presets::toJson(p);
    { std::ofstream o(path, std::ios::binary); o << "\xEF\xBB\xBF" << text; }
    presets::Preset q;
    auto r = presets::loadPresetFile(path, q);
    REQUIRE_MESSAGE(r.ok(), r.error);
    CHECK(q == p);
    CHECK_FALSE(presets::loadPresetFile(tmpDir() / "missing.radvoice.json", q).ok());
}

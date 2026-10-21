#include "radiff/presets/Preset.h"
#include "radiff/platform/Paths.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace radiff::presets {

using json = nlohmann::ordered_json;

namespace {
constexpr const char* kFormatTag = "radvoice";
}

std::string toJson(const Preset& preset) {
    json j;
    j["format"] = kFormatTag;
    j["format_version"] = kPresetFormatVersion;
    j["name"] = preset.name;
    j["description"] = preset.description;
    j["author"] = preset.author;
    json params = json::object();
    for (const auto& info : core::paramTable())
        params[std::string(info.key)] = preset.parameters.get(info.id);
    j["parameters"] = params;
    return j.dump(2) + "\n";
}

PresetResult fromJson(const std::string& text, Preset& preset) {
    PresetResult result;
    json j;
    try {
        j = json::parse(text);
    } catch (const json::exception& e) {
        result.error = std::string("Preset is not valid JSON: ") + e.what();
        return result;
    }
    if (!j.is_object()) {
        result.error = "Preset root must be a JSON object";
        return result;
    }
    if (!j.contains("format") || !j["format"].is_string() || j["format"].get<std::string>() != kFormatTag) {
        result.error = "Not a RaDiff preset (missing \"format\": \"radvoice\")";
        return result;
    }
    const int version = j.value("format_version", 0);
    if (version < 1 || version > kPresetFormatVersion) {
        result.error = "Unsupported preset format_version " + std::to_string(version) + " (this build supports 1.." +
                       std::to_string(kPresetFormatVersion) + ")";
        return result;
    }

    Preset out;
    out.name = j.value("name", std::string{});
    out.description = j.value("description", std::string{});
    out.author = j.value("author", std::string{});

    if (j.contains("parameters")) {
        const auto& params = j["parameters"];
        if (!params.is_object()) {
            result.error = "\"parameters\" must be an object";
            return result;
        }
        for (const auto& [key, value] : params.items()) {
            const auto id = core::paramIdForKey(key);
            if (!id) {
                result.warnings.push_back("Unknown parameter ignored: " + key);
                continue;
            }
            if (value.is_number()) {
                out.parameters.set(*id, value.get<float>());
            } else if (value.is_boolean()) {
                out.parameters.set(*id, value.get<bool>() ? 1.0f : 0.0f);
            } else {
                result.warnings.push_back("Parameter has non-numeric value and was ignored: " + key);
            }
        }
    }
    for (const auto& [key, value] : j.items()) {
        if (key != "format" && key != "format_version" && key != "name" && key != "description" &&
            key != "author" && key != "parameters")
            result.warnings.push_back("Unknown top-level key ignored: " + key);
    }
    preset = std::move(out);
    return result;
}

PresetResult loadPresetFile(const std::filesystem::path& file, Preset& preset) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        PresetResult r;
        r.error = "Cannot open preset file: " + platform::pathToUtf8(file);
        return r;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    // Tolerate a UTF-8 BOM, which Windows editors like to add.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        text.erase(0, 3);
    return fromJson(text, preset);
}

PresetResult savePresetFile(const std::filesystem::path& file, const Preset& preset) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    PresetResult r;
    if (!out) {
        r.error = "Cannot write preset file: " + platform::pathToUtf8(file);
        return r;
    }
    out << toJson(preset);
    if (!out)
        r.error = "Write failed for preset file: " + platform::pathToUtf8(file);
    return r;
}

} // namespace radiff::presets

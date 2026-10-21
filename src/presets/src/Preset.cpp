#include "radiff/presets/Preset.h"
#include "radiff/core/Engine.h"
#include "radiff/core/Version.h"
#include "radiff/platform/Paths.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <sstream>

namespace radiff::presets {

using json = nlohmann::ordered_json;
using core::Module;
using core::ParamId;
using core::ParamInfo;

namespace {

const std::vector<Module> kSerializedModules{Module::Input, Module::Eq, Module::Expander, Module::Compressor1,
                                             Module::Compressor2};

json encodeValue(const ParamInfo& info, float v) {
    if (info.isBoolean())
        return v >= 0.5f;
    if (info.isEnum())
        return std::string(core::enumName(info.id, v));
    // Round so that float noise never leaks into the file (0.7f -> 0.7, not 0.699999988).
    const double d = std::round(static_cast<double>(v) * 1e6) / 1e6;
    return d;
}

/// Returns false (with a warning) if the JSON value cannot be applied.
bool decodeValue(const ParamInfo& info, const json& j, core::ParameterSet& out, const std::string& where,
                 std::vector<std::string>& warnings) {
    if (j.is_boolean()) {
        out.set(info.id, j.get<bool>() ? 1.0f : 0.0f);
        return true;
    }
    if (j.is_number()) {
        out.set(info.id, j.get<float>());
        return true;
    }
    if (j.is_string()) {
        if (auto v = core::parseParamValue(info.id, j.get<std::string>())) {
            out.set(info.id, *v);
            return true;
        }
    }
    warnings.push_back("Ignored invalid value for " + where);
    return false;
}

void readModuleObject(Module m, int band, const json& obj, core::ParameterSet& out, const std::string& where,
                      std::vector<std::string>& warnings) {
    for (const auto& [key, value] : obj.items()) {
        if (m == Module::Eq && band < 0 && key == "bands") {
            if (!value.is_array()) {
                warnings.push_back("eq.bands must be an array; ignored");
                continue;
            }
            int i = 0;
            for (const auto& b : value) {
                if (i >= core::kNumEqBands) {
                    warnings.push_back("eq.bands has more than 5 entries; extras ignored");
                    break;
                }
                if (b.is_object())
                    readModuleObject(m, i, b, out, where + ".band" + std::to_string(i + 1), warnings);
                else
                    warnings.push_back("eq.bands[" + std::to_string(i) + "] is not an object; ignored");
                ++i;
            }
            continue;
        }
        const auto id = core::paramIdFor(m, band, key);
        if (!id) {
            warnings.push_back("Unknown parameter ignored: " + where + "." + key);
            continue;
        }
        decodeValue(core::paramInfo(*id), value, out, where + "." + key, warnings);
    }
}

} // namespace

const std::vector<std::string>& canonicalSignalOrder() {
    static const std::vector<std::string> order{"input", "eq", "expander", "compressor1", "compressor2", "output"};
    return order;
}

std::string toJson(const Preset& preset) {
    json j;
    j["schemaVersion"] = kPresetSchemaVersion;
    j["engineVersion"] = core::kVersionString;
    j["algorithmVersion"] = core::kAlgorithmVersion;
    j["name"] = preset.name;
    j["description"] = preset.description;
    j["author"] = preset.author;
    j["signalOrder"] = canonicalSignalOrder();
    j["bypass"] = preset.parameters.getBool(ParamId::Bypass);

    json modules = json::object();
    for (Module m : kSerializedModules) {
        json obj = json::object();
        json bands = json::array();
        for (int b = 0; b < (m == Module::Eq ? core::kNumEqBands : 0); ++b)
            bands.push_back(json::object());
        for (const auto& info : core::paramTable()) {
            if (info.module != m)
                continue;
            if (info.band >= 0)
                bands[static_cast<std::size_t>(info.band)][std::string(info.key)] =
                    encodeValue(info, preset.parameters.get(info.id));
            else
                obj[std::string(info.key)] = encodeValue(info, preset.parameters.get(info.id));
        }
        if (m == Module::Eq)
            obj["bands"] = bands;
        modules[std::string(core::moduleKey(m))] = obj;
    }
    j["modules"] = modules;

    json output = json::object();
    for (const auto& info : core::paramTable())
        if (info.module == Module::Output)
            output[std::string(info.key)] = encodeValue(info, preset.parameters.get(info.id));
    j["output"] = output;
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
    if (!j.contains("schemaVersion") || !j["schemaVersion"].is_number_integer()) {
        result.error = "Not a RaDiff preset (missing integer \"schemaVersion\")";
        return result;
    }
    const int version = j["schemaVersion"].get<int>();
    if (version < 1 || version > kPresetSchemaVersion) {
        result.error = "Unsupported preset schemaVersion " + std::to_string(version) + " (this build supports 1.." +
                       std::to_string(kPresetSchemaVersion) + ")";
        return result;
    }
    // Migration hook: when kPresetSchemaVersion grows, transform `j` here step by step.

    Preset out;
    out.name = j.value("name", std::string{});
    out.description = j.value("description", std::string{});
    out.author = j.value("author", std::string{});

    if (j.contains("signalOrder")) {
        if (!j["signalOrder"].is_array()) {
            result.error = "signalOrder must be an array of module names";
            return result;
        }
        std::vector<std::string> known;
        for (const auto& e : j["signalOrder"]) {
            if (!e.is_string())
                continue;
            const auto name = e.get<std::string>();
            if (core::moduleForKey(name) && name != "global")
                known.push_back(name);
            else
                result.warnings.push_back("signalOrder contains a module this build does not implement: " + name);
        }
        if (known != canonicalSignalOrder()) {
            result.error = "Unsupported signalOrder: this build processes a fixed chain (" +
                           [] { std::string s; for (const auto& k : canonicalSignalOrder()) s += (s.empty() ? "" : ", ") + k; return s; }() + ")";
            return result;
        }
    }

    for (const auto& [key, value] : j.items()) {
        if (key == "schemaVersion" || key == "engineVersion" || key == "algorithmVersion" || key == "name" ||
            key == "description" || key == "author" || key == "signalOrder")
            continue;
        if (key == "bypass") {
            decodeValue(core::paramInfo(ParamId::Bypass), value, out.parameters, "bypass", result.warnings);
        } else if (key == "output") {
            if (value.is_object())
                readModuleObject(Module::Output, -1, value, out.parameters, "output", result.warnings);
            else
                result.warnings.push_back("output must be an object; ignored");
        } else if (key == "modules") {
            if (!value.is_object()) {
                result.error = "\"modules\" must be an object";
                return result;
            }
            for (const auto& [mname, mobj] : value.items()) {
                const auto m = core::moduleForKey(mname);
                if (!m || *m == Module::Global || *m == Module::Output) {
                    result.warnings.push_back("Unknown module ignored: " + mname);
                    continue;
                }
                if (!mobj.is_object()) {
                    result.warnings.push_back("Module " + mname + " is not an object; ignored");
                    continue;
                }
                readModuleObject(*m, -1, mobj, out.parameters, mname, result.warnings);
            }
        } else {
            result.warnings.push_back("Unknown top-level key ignored: " + key);
        }
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

#include "radiff/ui/ParamText.h"

#include <cmath>
#include <cstdio>

namespace radiff::ui {

namespace {
std::string number(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

int decimalsFor(const core::ParamInfo& info) {
    switch (info.unit) {
    case core::ParamUnit::Hertz: return info.maxValue >= 1000.0f ? 0 : 1;
    case core::ParamUnit::Milliseconds: return info.minValue < 1.0f ? 2 : 1;
    case core::ParamUnit::Fraction:
    case core::ParamUnit::Q:
    case core::ParamUnit::Octaves: return 2;
    case core::ParamUnit::Ratio: return 2;
    case core::ParamUnit::Decibels: return 1;
    case core::ParamUnit::DbPerOctave: return 0;
    case core::ParamUnit::None: return 1;
    case core::ParamUnit::Boolean:
    case core::ParamUnit::Enum: return 0;
    }
    return 1;
}
} // namespace

std::string rangeText(const core::ParamInfo& info) {
    if (info.isBoolean())
        return "on / off";
    if (info.isEnum()) {
        std::string s;
        for (auto n : info.enumNames)
            s += (s.empty() ? "" : " | ") + std::string(n);
        return s;
    }
    const int d = info.step > 0.0f ? 0 : decimalsFor(info);
    std::string s = number(static_cast<double>(info.minValue), d) + " … " + number(static_cast<double>(info.maxValue), d);
    const auto unit = core::unitSuffix(info.unit);
    if (!unit.empty())
        s += " " + std::string(unit);
    return s;
}

std::string tooltipText(core::ParamId id) {
    const auto& info = core::paramInfo(id);
    return std::string(info.name) + "  (" + core::paramPath(id) + ")\n" + rangeText(info);
}

std::string shortLabel(const core::ParamInfo& info) {
    std::string name(info.name);
    static constexpr const char* prefixes[] = {"Exp ", "Comp 1 ", "Comp 2 ", "De-ess ", "Enhancer ", "Input ", "EQ "};
    for (const char* p : prefixes) {
        const std::string pre(p);
        if (name.rfind(pre, 0) == 0) {
            name.erase(0, pre.size());
            break;
        }
    }
    // "3 Gain" (EQ band rows carry their own band label) -> "Gain"
    if (info.band >= 0 && name.size() > 2 && name[0] >= '1' && name[0] <= '5' && name[1] == ' ')
        name.erase(0, 2);
    return name;
}

std::string valueText(const core::ParamInfo& info, double value) {
    if (info.isBoolean())
        return value >= 0.5 ? "on" : "off";
    if (info.isEnum())
        return std::string(core::enumName(info.id, static_cast<float>(value)));
    std::string s = number(value, decimalsFor(info));
    const auto unit = core::unitSuffix(info.unit);
    if (!unit.empty())
        s += " " + std::string(unit);
    return s;
}

} // namespace radiff::ui

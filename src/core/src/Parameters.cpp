#include "radiff/core/Parameters.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace radiff::core {

namespace {

constexpr std::array<std::string_view, kNumModules> kModuleKeys{"global",      "input",       "eq",    "expander",
                                                                "compressor1", "compressor2", "output"};

constexpr std::array<ParamInfo, kNumParams> kTable{{
#define RADIFF_PARAM_ROW(Id, Mod, Band, Key, Name, Unit, Min, Max, Def, Step, Enum)                                    \
    {ParamId::Id, Module::Mod, Band, Key, Name, ParamUnit::Unit, static_cast<float>(Min), static_cast<float>(Max),     \
     static_cast<float>(Def), static_cast<float>(Step), std::span<const std::string_view>(Enum)},
    RADIFF_PARAM_LIST(RADIFF_PARAM_ROW)
#undef RADIFF_PARAM_ROW
}};

constexpr bool tableIsOrdered() {
    for (std::size_t i = 0; i < kTable.size(); ++i)
        if (static_cast<std::size_t>(kTable[i].id) != i)
            return false;
    return true;
}
static_assert(tableIsOrdered(), "paramTable() must list parameters in ParamId order");

constexpr bool defaultsInRange() {
    for (const auto& p : kTable)
        if (p.defaultValue < p.minValue || p.defaultValue > p.maxValue)
            return false;
    return true;
}
static_assert(defaultsInRange(), "every parameter default must lie within its range");

bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

} // namespace

std::string_view moduleKey(Module m) noexcept { return kModuleKeys[static_cast<std::size_t>(m)]; }

std::optional<Module> moduleForKey(std::string_view key) noexcept {
    for (std::size_t i = 0; i < kNumModules; ++i)
        if (kModuleKeys[i] == key)
            return static_cast<Module>(i);
    return std::nullopt;
}

const std::array<ParamInfo, kNumParams>& paramTable() noexcept { return kTable; }
const ParamInfo& paramInfo(ParamId id) noexcept { return kTable[static_cast<std::size_t>(id)]; }

std::string paramPath(ParamId id) {
    const auto& p = paramInfo(id);
    std::string s(moduleKey(p.module));
    if (p.band >= 0)
        s += ".band" + std::to_string(p.band + 1);
    s += '.';
    s += p.key;
    return s;
}

std::optional<ParamId> paramIdFor(Module m, int band, std::string_view key) noexcept {
    for (const auto& p : kTable)
        if (p.module == m && p.band == band && p.key == key)
            return p.id;
    return std::nullopt;
}

std::optional<ParamId> paramIdForPath(std::string_view path) noexcept {
    const auto dot = path.find('.');
    if (dot == std::string_view::npos)
        return std::nullopt;
    const auto mod = moduleForKey(path.substr(0, dot));
    if (!mod)
        return std::nullopt;
    std::string_view rest = path.substr(dot + 1);
    int band = -1;
    if (rest.rfind("band", 0) == 0) {
        const auto dot2 = rest.find('.');
        if (dot2 == std::string_view::npos || dot2 < 5)
            return std::nullopt;
        const std::string_view num = rest.substr(4, dot2 - 4);
        int n = 0;
        for (char c : num) {
            if (c < '0' || c > '9')
                return std::nullopt;
            n = n * 10 + (c - '0');
        }
        if (n < 1 || n > kNumEqBands)
            return std::nullopt;
        band = n - 1;
        rest = rest.substr(dot2 + 1);
    }
    return paramIdFor(*mod, band, rest);
}

std::optional<float> parseParamValue(ParamId id, std::string_view text) noexcept {
    const auto& p = paramInfo(id);
    if (p.isEnum()) {
        for (std::size_t i = 0; i < p.enumNames.size(); ++i)
            if (iequals(p.enumNames[i], text))
                return static_cast<float>(i);
    }
    if (p.isBoolean()) {
        if (iequals(text, "true") || iequals(text, "on") || iequals(text, "yes"))
            return 1.0f;
        if (iequals(text, "false") || iequals(text, "off") || iequals(text, "no"))
            return 0.0f;
    }
    // Plain number, locale independent (digits, sign, '.', exponent only).
    if (text.empty())
        return std::nullopt;
    char buf[64];
    if (text.size() >= sizeof(buf))
        return std::nullopt;
    std::size_t n = 0;
    for (char c : text) {
        const bool ok = (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E';
        if (!ok)
            return std::nullopt;
        buf[n++] = c;
    }
    buf[n] = '\0';
    char* end = nullptr;
    const double v = std::strtod(buf, &end);
    if (end != buf + n || !std::isfinite(v))
        return std::nullopt;
    return static_cast<float>(v);
}

std::string_view enumName(ParamId id, float value) noexcept {
    const auto& p = paramInfo(id);
    if (!p.isEnum() || p.enumNames.empty())
        return {};
    const auto i = static_cast<std::size_t>(std::clamp(static_cast<int>(value + 0.5f), 0,
                                                       static_cast<int>(p.enumNames.size()) - 1));
    return p.enumNames[i];
}

ParameterSet::ParameterSet() {
    for (const auto& p : kTable)
        values_[static_cast<std::size_t>(p.id)] = p.defaultValue;
}

void ParameterSet::set(ParamId id, float value) noexcept {
    const auto& info = paramInfo(id);
    if (!std::isfinite(value))
        value = info.defaultValue;
    if (info.step > 0.0f)
        value = info.minValue + std::round((value - info.minValue) / info.step) * info.step;
    values_[static_cast<std::size_t>(id)] = std::clamp(value, info.minValue, info.maxValue);
}

ParamId ParameterSet::eqParam(int band, std::string_view key) noexcept {
    const auto id = paramIdFor(Module::Eq, band, key);
    return id ? *id : ParamId::EqEnabled;
}

} // namespace radiff::core
